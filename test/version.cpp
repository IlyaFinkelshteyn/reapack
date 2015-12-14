#include <catch.hpp>

#include <version.hpp>

#include <database.hpp>
#include <errors.hpp>
#include <package.hpp>

using namespace std;

static const char *M = "[version]";

TEST_CASE("invalid", M) {
  try {
    Version ver("hello");
    FAIL();
  }
  catch(const reapack_error &e) {
    REQUIRE(string(e.what()) == "invalid version name");
  }
}

TEST_CASE("major minor patch version", M) {
  Version ver("1.2.3");
  REQUIRE(ver.name() == "1.2.3");
  REQUIRE(ver.code() == 1000200030000);
}

TEST_CASE("major minor version", M) {
  Version ver("1.2");
  REQUIRE(ver.name() == "1.2");
  REQUIRE(ver.code() == 1000200000000);
}

TEST_CASE("major version", M) {
  Version ver("1");
  REQUIRE(ver.name() == "1");
  REQUIRE(ver.code() == 1000000000000);
}

TEST_CASE("version with string suffix", M) {
  Version ver("1.2pre3");
  REQUIRE(ver.name() == "1.2pre3");
  REQUIRE(ver.code() == 1000200030000);
}

TEST_CASE("version with 4 components", M) {
  Version ver("1.2.3.4");
  REQUIRE(ver.name() == "1.2.3.4");
  REQUIRE(ver.code() == 1000200030004);
  REQUIRE(ver < Version("1.2.4"));
}

TEST_CASE("4 digits version component", M) {
  Version ver("0.2015.12.25");
  REQUIRE(ver.name() == "0.2015.12.25");
  REQUIRE(ver.code() == 201500120025);
}

TEST_CASE("5 digits version component", M) {
  try {
    Version ver("12345.1");
    FAIL();
  }
  catch(const reapack_error &e) {
    REQUIRE(string(e.what()) == "version component overflow");
  }
}

TEST_CASE("version with 5 components", M) {
  try {
    Version ver("1.2.3.4.5");
    FAIL();
  }
  catch(const reapack_error &e) {
    REQUIRE(string(e.what()) == "invalid version name");
  }
}

TEST_CASE("version full name", M) {
  Version ver("1.0");
  REQUIRE(ver.fullName() == "v1.0");

  Package pkg(Package::UnknownType, "file.name");
  ver.setPackage(&pkg);
  REQUIRE(ver.fullName() == "file.name v1.0");

  Category cat("Category Name");
  pkg.setCategory(&cat);
  REQUIRE(ver.fullName() == "Category Name/file.name v1.0");

  Database db;
  db.setName("Database Name");
  cat.setDatabase(&db);
  REQUIRE(ver.fullName() == "Database Name/Category Name/file.name v1.0");
}

TEST_CASE("add source", M) {
  Source *src = new Source(Source::GenericPlatform, "a", "b");

  Version ver("1");
  CHECK(ver.sources().size() == 0);
  ver.addSource(src);
  CHECK(ver.sources().size() == 1);

  REQUIRE(src->version() == &ver);
  REQUIRE(ver.source(0) == src);
}

TEST_CASE("list files", M) {
  Source *src1 = new Source(Source::GenericPlatform, "file", "url");

  Version ver("1");
  ver.addSource(src1);

  Package pkg(Package::ScriptType, "name");
  ver.setPackage(&pkg);

  Category cat("Category Name");
  pkg.setCategory(&cat);

  Database db;
  db.setName("Database Name");
  cat.setDatabase(&db);

  Path path1;
  path1.append("Scripts");
  path1.append("Database Name");
  path1.append("Category Name");
  path1.append("file");

  const vector<Path> expected{path1};
  REQUIRE(ver.files() == expected);
}

TEST_CASE("drop sources for unknown platforms", M) {
  Version ver("1");
  ver.addSource(new Source(Source::UnknownPlatform, "a", "b"));

  REQUIRE(ver.sources().size() == 0);
}

#ifdef __APPLE__
TEST_CASE("drop windows sources on os x", M) {
  Version ver("1");
  ver.addSource(new Source(Source::WindowsPlatform, "a", "b"));
  ver.addSource(new Source(Source::Win32Platform, "a", "b"));
  ver.addSource(new Source(Source::Win64Platform, "a", "b"));

  REQUIRE(ver.sources().size() == 0);
}

#ifdef __x86_64__
TEST_CASE("drop 32-bit sources on os x 64-bit", M) {
  Version ver("1");
  ver.addSource(new Source(Source::Darwin32Platform, "a", "b"));

  REQUIRE(ver.sources().size() == 0);
}

TEST_CASE("valid sources for os x 64-bit", M) {
  Version ver("1");
  ver.addSource(new Source(Source::GenericPlatform, "a", "b"));
  ver.addSource(new Source(Source::DarwinPlatform, "a", "b"));
  ver.addSource(new Source(Source::Darwin64Platform, "a", "b"));

  REQUIRE(ver.sources().size() == 3);
}
#else
TEST_CASE("drop 64-bit sources on os x 32-bit", M) {
  Version ver("1");
  ver.addSource(new Source(Source::Darwin64Platform, "a", "b"));

  REQUIRE(ver.sources().size() == 0);
}

TEST_CASE("valid sources for os x 32-bit", M) {
  Version ver("1");
  ver.addSource(new Source(Source::GenericPlatform, "a", "b"));
  ver.addSource(new Source(Source::DarwinPlatform, "a", "b"));
  ver.addSource(new Source(Source::Darwin32Platform, "a", "b"));

  REQUIRE(ver.sources().size() == 3);
}
#endif

#elif _WIN32
TEST_CASE("drop os x sources on windows", M) {
  Version ver("1");
  ver.addSource(new Source(Source::DarwinPlatform, "a", "b"));
  ver.addSource(new Source(Source::Darwin32Platform, "a", "b"));
  ver.addSource(new Source(Source::Darwin64Platform, "a", "b"));

  REQUIRE(ver.sources().size() == 0);
}

#ifdef _WIN64
TEST_CASE("drop 32-bit sources on windows 64-bit", M) {
  Version ver("1");
  ver.addSource(new Source(Source::Win32Platform, "a", "b"));

  REQUIRE(ver.sources().size() == 0);
}

TEST_CASE("valid sources for windows 64-bit", M) {
  Version ver("1");
  ver.addSource(new Source(Source::GenericPlatform, "a", "b"));
  ver.addSource(new Source(Source::WindowsPlatform, "a", "b"));
  ver.addSource(new Source(Source::Win64Platform, "a", "b"));

  REQUIRE(ver.sources().size() == 3);
}
#else
TEST_CASE("drop 64-bit sources on windows 32-bit", M) {
  Version ver("1");
  ver.addSource(new Source(Source::Win64Platform, "a", "b"));

  REQUIRE(ver.sources().size() == 0);
}

TEST_CASE("valid sources for windows 32-bit", M) {
  Version ver("1");
  ver.addSource(new Source(Source::GenericPlatform, "a", "b"));
  ver.addSource(new Source(Source::WindowsPlatform, "a", "b"));
  ver.addSource(new Source(Source::Win32Platform, "a", "b"));

  REQUIRE(ver.sources().size() == 3);
}
#endif
#endif
