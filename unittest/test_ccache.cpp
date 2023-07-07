// Copyright (C) 2020-2023 Joel Rosdahl and other contributors
//
// See doc/AUTHORS.adoc for a complete list of contributors.
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation; either version 3 of the License, or (at your option)
// any later version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
// more details.
//
// You should have received a copy of the GNU General Public License along with
// this program; if not, write to the Free Software Foundation, Inc., 51
// Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA

#include "../src/Context.hpp"
#include "../src/ccache.hpp"
#include "../src/fmtmacros.hpp"
#include "TestUtil.hpp"

#include <Util.hpp>
#include <core/wincompat.hpp>
#include <util/file.hpp>

#include "third_party/doctest.h"

#include <optional>

#ifdef HAVE_UNISTD_H
#  include <unistd.h>
#endif

using TestUtil::TestContext;

TEST_SUITE_BEGIN("ccache");

// Wraps find_compiler in a test friendly interface.
static std::string
helper(bool masquerading_as_compiler,
       const char* args,
       const char* config_compiler,
       const char* find_executable_return_string = nullptr)
{
  const auto find_executable_stub =
    [&find_executable_return_string](const auto&, const auto& s, const auto&) {
      return find_executable_return_string ? find_executable_return_string
                                           : "resolved_" + s;
    };

  Context ctx;
  ctx.config.set_compiler(config_compiler);
  ctx.orig_args = Args::from_string(args);
  find_compiler(ctx, find_executable_stub, masquerading_as_compiler);
  return ctx.orig_args.to_string();
}

TEST_CASE("split_argv")
{
  ArgvParts argv_parts;

  SUBCASE("empty")
  {
    argv_parts = split_argv(0, nullptr);
    CHECK(argv_parts.masquerading_as_compiler);
    CHECK(argv_parts.config_settings.empty());
    CHECK(argv_parts.compiler_and_args.empty());
  }

  SUBCASE("ccache")
  {
    const char* const argv[] = {"ccache"};
    argv_parts = split_argv(std::size(argv), argv);
    CHECK(!argv_parts.masquerading_as_compiler);
    CHECK(argv_parts.config_settings.empty());
    CHECK(argv_parts.compiler_and_args.empty());
  }

  SUBCASE("normal compilation")
  {
    const char* const argv[] = {"ccache", "gcc", "-c", "test.c"};
    argv_parts = split_argv(std::size(argv), argv);
    CHECK(!argv_parts.masquerading_as_compiler);
    CHECK(argv_parts.config_settings.empty());
    CHECK(argv_parts.compiler_and_args == Args::from_string("gcc -c test.c"));
  }

  SUBCASE("only config options")
  {
    const char* const argv[] = {"ccache", "foo=bar"};
    argv_parts = split_argv(std::size(argv), argv);
    CHECK(!argv_parts.masquerading_as_compiler);
    CHECK(argv_parts.config_settings == std::vector<std::string>{"foo=bar"});
    CHECK(argv_parts.compiler_and_args.empty());
  }

  SUBCASE("compilation with config options")
  {
    const char* const argv[] = {"ccache", "a=b", "c = d", "/usr/bin/gcc"};
    argv_parts = split_argv(std::size(argv), argv);
    CHECK(!argv_parts.masquerading_as_compiler);
    CHECK(argv_parts.config_settings
          == std::vector<std::string>{"a=b", "c = d"});
    CHECK(argv_parts.compiler_and_args == Args::from_string("/usr/bin/gcc"));
  }
}

TEST_CASE("find_compiler")
{
  SUBCASE("no config")
  {
    // In case the first parameter is gcc it must be a link to ccache, so
    // find_compiler should call find_executable to locate the next best "gcc"
    // and return that value.
    CHECK(helper(true, "gcc", "") == "resolved_gcc");
    CHECK(helper(true, "relative/gcc", "") == "resolved_gcc");
    CHECK(helper(true, "/absolute/gcc", "") == "resolved_gcc");

    // In case the first parameter is ccache, resolve the second parameter to
    // the real compiler unless it's a relative or absolute path.
    CHECK(helper(false, "gcc", "") == "resolved_gcc");
    CHECK(helper(false, "rel/gcc", "") == "rel/gcc");
    CHECK(helper(false, "/abs/gcc", "") == "/abs/gcc");

    // If gcc points back to ccache throw, unless either ccache or gcc is a
    // relative or absolute path.
    CHECK_THROWS(helper(false, "gcc", "", "ccache"));
    CHECK(helper(false, "rel/gcc", "", "ccache") == "rel/gcc");
    CHECK(helper(false, "/abs/gcc", "", "ccache") == "/abs/gcc");

    // If compiler is not found then throw, unless the compiler has a relative
    // or absolute path.
    CHECK_THROWS(helper(false, "gcc", "", ""));
    CHECK(helper(false, "rel/gcc", "", "") == "rel/gcc");
    CHECK(helper(false, "/abs/gcc", "", "") == "/abs/gcc");
  }

  SUBCASE("config")
  {
    // In case the first parameter is gcc it must be a link to ccache so use
    // config value instead. Don't resolve config if it's a relative or absolute
    // path.
    CHECK(helper(true, "gcc", "config") == "resolved_config");
    CHECK(helper(true, "gcc", "rel/config") == "rel/config");
    CHECK(helper(true, "gcc", "/abs/config") == "/abs/config");
    CHECK(helper(true, "rel/gcc", "config") == "resolved_config");
    CHECK(helper(true, "rel/gcc", "rel/config") == "rel/config");
    CHECK(helper(true, "rel/gcc", "/abs/config") == "/abs/config");
    CHECK(helper(true, "/abs/gcc", "config") == "resolved_config");
    CHECK(helper(true, "/abs/gcc", "rel/config") == "rel/config");
    CHECK(helper(true, "/abs/gcc", "/abs/config") == "/abs/config");

    // In case the first parameter is ccache, use the configuration value. Don't
    // resolve configuration value if it's a relative or absolute path.
    CHECK(helper(false, "gcc", "config") == "resolved_config");
    CHECK(helper(false, "gcc", "rel/config") == "rel/config");
    CHECK(helper(false, "gcc", "/abs/config") == "/abs/config");
    CHECK(helper(false, "rel/gcc", "config") == "resolved_config");
    CHECK(helper(false, "/abs/gcc", "config") == "resolved_config");
  }
}

TEST_CASE("guess_compiler")
{
  TestContext test_context;

  SUBCASE("Compiler not in file system")
  {
    CHECK(guess_compiler("/test/prefix/clang") == CompilerType::clang);
    CHECK(guess_compiler("/test/prefix/clang-3.8") == CompilerType::clang);
    CHECK(guess_compiler("/test/prefix/clang++") == CompilerType::clang);
    CHECK(guess_compiler("/test/prefix/clang++-10") == CompilerType::clang);

    CHECK(guess_compiler("/test/prefix/gcc") == CompilerType::gcc);
    CHECK(guess_compiler("/test/prefix/gcc-4.8") == CompilerType::gcc);
    CHECK(guess_compiler("/test/prefix/g++") == CompilerType::gcc);
    CHECK(guess_compiler("/test/prefix/g++-9") == CompilerType::gcc);
    CHECK(guess_compiler("/test/prefix/x86_64-w64-mingw32-gcc-posix")
          == CompilerType::gcc);

    CHECK(guess_compiler("/test/prefix/nvcc") == CompilerType::nvcc);
    CHECK(guess_compiler("/test/prefix/nvcc-10.1.243") == CompilerType::nvcc);

    CHECK(guess_compiler("/test/prefix/x") == CompilerType::other);
    CHECK(guess_compiler("/test/prefix/cc") == CompilerType::other);
    CHECK(guess_compiler("/test/prefix/c++") == CompilerType::other);
  }

#ifndef _WIN32
  SUBCASE("Follow symlink to actual compiler")
  {
    const auto cwd = Util::get_actual_cwd();
    util::write_file(FMT("{}/gcc", cwd), "");
    CHECK(symlink("gcc", FMT("{}/intermediate", cwd).c_str()) == 0);
    const auto cc = FMT("{}/cc", cwd);
    CHECK(symlink("intermediate", cc.c_str()) == 0);

    CHECK(guess_compiler(cc) == CompilerType::gcc);
  }
#endif
}

TEST_SUITE_END();
