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

#include "Win32Util.hpp"

#include "Util.hpp"

#include <util/string.hpp>

#include <chrono>
#include <thread>

namespace Win32Util {

std::string
add_exe_suffix(const std::string& path)
{
  auto ext = util::to_lowercase(Util::get_extension(path));
  if (ext == ".exe" || ext == ".bat" || ext == ".sh") {
    return path;
  } else {
    return path + ".exe";
  }
}

std::string
error_message(DWORD error_code)
{
  LPSTR buffer;
  size_t size =
    FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM
                     | FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr,
                   error_code,
                   MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                   reinterpret_cast<LPSTR>(&buffer),
                   0,
                   nullptr);
  std::string message(buffer, size);
  while (!message.empty()
         && (message.back() == '\n' || message.back() == '\r')) {
    message.pop_back();
  }
  LocalFree(buffer);
  return message;
}

std::string
argv_to_string(const char* const* argv,
               const std::string& prefix,
               bool escape_backslashes)
{
  std::string result;
  size_t i = 0;
  const char* arg = prefix.empty() ? argv[i++] : prefix.c_str();

  do {
    int bs = 0;
    result += '"';
    for (size_t j = 0; arg[j]; ++j) {
      switch (arg[j]) {
      case '\\':
        if (!escape_backslashes) {
          ++bs;
          break;
        }
        [[fallthrough]];

      case '"':
        bs = (bs << 1) + 1;
        [[fallthrough]];

      default:
        while (bs > 0) {
          result += '\\';
          --bs;
        }
        result += arg[j];
      }
    }
    bs <<= 1;
    while (bs > 0) {
      result += '\\';
      --bs;
    }
    result += "\" ";
  } while ((arg = argv[i++]));

  result.resize(result.length() - 1);
  return result;
}

} // namespace Win32Util
