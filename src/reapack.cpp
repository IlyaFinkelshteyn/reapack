/* ReaPack: Package manager for REAPER
 * Copyright (C) 2015-2018  Christian Fillion
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "reapack.hpp"

#include "about.hpp"
#include "api.hpp"
#include "browser.hpp"
#include "config.hpp"
#include "download.hpp"
#include "errors.hpp"
#include "filesystem.hpp"
#include "index.hpp"
#include "manager.hpp"
#include "obsquery.hpp"
#include "progress.hpp"
#include "report.hpp"
#include "richedit.hpp"
#include "transaction.hpp"
#include "win32.hpp"

#include <reaper_plugin_functions.h>

using namespace std;

const char *ReaPack::VERSION = "1.2.1";
const char *ReaPack::BUILDTIME = __DATE__ " " __TIME__;

ReaPack *ReaPack::s_instance = nullptr;

#ifdef _WIN32
// Removes temporary files that could not be removed by an installation task
// (eg. extensions dll that were in use by REAPER).
// Surely there must be a better way...
static void CleanupTempFiles()
{
  const Path &path = (Path::DATA + "*.tmp").prependRoot();
  const wstring &pattern = Win32::widen(path.join());

  WIN32_FIND_DATA fd = {};
  HANDLE handle = FindFirstFile(pattern.c_str(), &fd);

  if(handle == INVALID_HANDLE_VALUE)
    return;

  do {
    wstring file = pattern;
    file.replace(file.size() - 5, 5, fd.cFileName); // 5 == strlen("*.tmp")
    DeleteFile(file.c_str());
  } while(FindNextFile(handle, &fd));

  FindClose(handle);
}
#endif

Path ReaPack::resourcePath()
{
#ifdef _WIN32
  // convert from the current system codepage to UTF-8
  if(atof(GetAppVersion()) < 5.70)
    return Win32::narrow(Win32::widen(GetResourcePath(), CP_ACP));
#endif

  return {GetResourcePath()};
}

ReaPack::ReaPack(REAPER_PLUGIN_HINSTANCE instance, HWND mainWindow)
  : m_instance(instance), m_mainWindow(mainWindow),
    m_useRootPath(resourcePath()), m_config(Path::CONFIG.prependRoot()),
    m_tx{}, m_about{}, m_browser{}, m_manager{}, m_progress{}
{
  assert(!s_instance);
  s_instance = this;

  DownloadContext::GlobalInit();
  RichEdit::Init();

  createDirectories();
  registerSelf();
  setupActions();
  setupAPI();

  if(m_config.isFirstRun())
    manageRemotes();

#ifdef _WIN32
  CleanupTempFiles();
#endif
}

ReaPack::~ReaPack()
{
  Dialog::DestroyAll();
  DownloadContext::GlobalCleanup();

  s_instance = nullptr;
}

void ReaPack::setupActions()
{
  m_actions.add("REAPACK_SYNC", "ReaPack: Synchronize packages",
    std::bind(&ReaPack::synchronizeAll, this));

  m_actions.add("REAPACK_BROWSE", "ReaPack: Browse packages...",
    std::bind(&ReaPack::browsePackages, this));

  m_actions.add("REAPACK_IMPORT", "ReaPack: Import repositories...",
    std::bind(&ReaPack::importRemote, this));

  m_actions.add("REAPACK_MANAGE", "ReaPack: Manage repositories...",
    std::bind(&ReaPack::manageRemotes, this));

  m_actions.add("REAPACK_ABOUT", "ReaPack: About...",
    std::bind(&ReaPack::aboutSelf, this));
}

void ReaPack::setupAPI()
{
  m_api.emplace_back(&API::AboutInstalledPackage);
  m_api.emplace_back(&API::AboutRepository);
  m_api.emplace_back(&API::AddSetRepository);
  m_api.emplace_back(&API::BrowsePackages);
  m_api.emplace_back(&API::CompareVersions);
  m_api.emplace_back(&API::EnumOwnedFiles);
  m_api.emplace_back(&API::FreeEntry);
  m_api.emplace_back(&API::GetEntryInfo);
  m_api.emplace_back(&API::GetOwner);
  m_api.emplace_back(&API::GetRepositoryInfo);
  m_api.emplace_back(&API::ProcessQueue);
}

void ReaPack::synchronizeAll()
{
  const auto &remotes = m_config.remotes.enabled();

  if(remotes.empty()) {
    ShowMessageBox("No repository enabled, nothing to do!", "ReaPack", MB_OK);
    return;
  }

  Transaction *tx = setupTransaction();

  if(!tx)
    return;

  for(const RemotePtr &remote : remotes)
    tx->synchronize(remote);

  tx->runTasks();
}

void ReaPack::importRemote()
{
  const bool autoClose = m_manager == nullptr;

  manageRemotes();

  if(!m_manager->importRepo() && autoClose)
    m_manager->close();
}

void ReaPack::manageRemotes()
{
  if(m_manager) {
    m_manager->setFocus();
    return;
  }

  m_manager = Dialog::Create<Manager>(m_instance, m_mainWindow);
  m_manager->show();

  m_manager->setCloseHandler([=] (INT_PTR) {
    Dialog::Destroy(m_manager);
    m_manager = nullptr;
  });
}

void ReaPack::aboutSelf()
{
  if(const RemotePtr &self = m_config.remotes.getSelf())
    self->about();
}

About *ReaPack::about(const bool instantiate)
{
  if(m_about)
    return m_about;
  else if(!instantiate)
    return nullptr;

  m_about = Dialog::Create<About>(m_instance, m_mainWindow);

  m_about->setCloseHandler([=] (INT_PTR) {
    Dialog::Destroy(m_about);
    m_about = nullptr;
  });

  return m_about;
}

Browser *ReaPack::browsePackages()
{
  if(m_browser) {
    m_browser->setFocus();
    return m_browser;
  }

  m_browser = Dialog::Create<Browser>(m_instance, m_mainWindow);
  m_browser->refresh();
  m_browser->setCloseHandler([=] (INT_PTR) {
    Dialog::Destroy(m_browser);
    m_browser = nullptr;
  });

  return m_browser;
}

Transaction *ReaPack::setupTransaction()
{
  if(m_progress && m_progress->isVisible())
    m_progress->setFocus();

  if(m_tx)
    return m_tx;

  try {
    m_tx = new Transaction;
  }
  catch(const reapack_error &e) {
    Win32::messageBox(m_mainWindow, String::format(
      "The following error occurred while creating a transaction:\n\n%s",
      e.what()
    ).c_str(), "ReaPack", MB_OK);
    return nullptr;
  }

  assert(!m_progress);
  m_progress = Dialog::Create<Progress>(m_instance, m_mainWindow, m_tx->threadPool());

  m_tx->onFinish([=] {
    Dialog::Destroy(m_progress);
    m_progress = nullptr;

    if(!m_tx->isCancelled() && !m_tx->receipt()->empty()) {
      LockDialog managerLock(m_manager);
      LockDialog browserLock(m_browser);

      Dialog::Show<Report>(m_instance, m_mainWindow, m_tx->receipt());
    }
  });

  m_tx->setObsoleteHandler([=] (vector<Registry::Entry> &entries) {
    LockDialog aboutLock(m_about);
    LockDialog browserLock(m_browser);
    LockDialog managerLock(m_manager);
    LockDialog progressLock(m_progress);

    return Dialog::Show<ObsoleteQuery>(m_instance, m_mainWindow,
      &entries, &config()->install.promptObsolete) == IDOK;
  });

  m_tx->setCleanupHandler(bind(&ReaPack::teardownTransaction, this));

  return m_tx;
}

void ReaPack::teardownTransaction()
{
  const bool needRefresh = m_tx->receipt()->test(Receipt::RefreshBrowser);

  delete m_tx;
  m_tx = nullptr;

  // Update the browser only after the transaction is deleted because
  // it must be able to start a new one to load the indexes
  if(needRefresh)
    refreshBrowser();
}

void ReaPack::commitConfig(bool refresh)
{
  if(m_tx) {
    if(refresh) {
      m_tx->receipt()->setIndexChanged(); // force browser refresh
      m_tx->onFinish(bind(&ReaPack::refreshManager, this));
    }
    m_tx->onFinish(bind(&Config::write, &m_config));
    m_tx->runTasks();
  }
  else {
    if(refresh) {
      refreshManager();
      refreshBrowser();
    }
    m_config.write();
  }
}

void ReaPack::refreshManager()
{
  if(m_manager)
    m_manager->refresh();
}

void ReaPack::refreshBrowser()
{
  if(m_browser)
    m_browser->refresh();
}

void ReaPack::createDirectories()
{
  const Path &path = Path::CACHE;

  if(FS::mkdir(path))
    return;

  Win32::messageBox(Splash_GetWnd(), String::format(
    "ReaPack could not create %s! "
    "Please investigate or report this issue.\n\n"
    "Error description: %s",
    path.prependRoot().join().c_str(), FS::lastError()
  ).c_str(), "ReaPack", MB_OK);
}

void ReaPack::registerSelf()
{
  // hard-coding galore!
  Index ri(m_config.remotes.getSelf());
  Category cat("Extensions", &ri);
  Package pkg(Package::ExtensionType, "ReaPack.ext", &cat);
  Version ver(VERSION, &pkg);
  ver.setAuthor("cfillion");
  ver.addSource(new Source(REAPACK_FILE, "dummy url", &ver));

  try {
    Registry reg(Path::REGISTRY.prependRoot());
    reg.push(&ver);
    reg.commit();
  }
  catch(const reapack_error &) {
    // Best to ignore the error for now. If something is wrong with the registry
    // we'll show a message once when the user really wants to interact with ReaPack.
    //
    // Right now the user is likely to just want to use REAPER without being bothered.
  }
}
