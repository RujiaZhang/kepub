#include "util.h"

#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <tuple>
#include <unordered_set>

#include <klib/log.h>
#include <klib/unicode.h>
#include <klib/util.h>
#include <unicode/regex.h>
#include <unicode/uchar.h>
#include <boost/algorithm/string.hpp>
#include <gsl/assert>

#include "trans.h"

namespace kepub {

namespace {

bool start_with_chinese(const std::string &str) {
  return klib::is_chinese(klib::first_code_point(str));
}

bool end_with_chinese(const std::string &str) {
  return klib::is_chinese(klib::last_code_point(str));
}

bool is_punctuation(char32_t code_point) {
  return code_point == U'◇' || klib::is_chinese_punctuation(code_point);
}

bool end_with_punctuation(const std::string &str) {
  return klib::is_chinese_punctuation(klib::last_code_point(str));
}

std::string make_book_name_legal(const std::string &file_name) {
  auto new_file_name = klib::make_file_name_legal(file_name);
  if (new_file_name != file_name) {
    klib::warn(
        "The title of the book is illegal, and the title of the book has been "
        "changed from '{}' to '{}'",
        file_name, new_file_name);
  }

  return new_file_name;
}

bool regex_match(const std::string &str, const std::string &regex) {
  auto input = icu::UnicodeString::fromUTF8(str);
  auto regex_str = icu::UnicodeString::fromUTF8(regex);

  UErrorCode status = U_ZERO_ERROR;
  icu::RegexMatcher regex_matcher(
      regex_str, input, UREGEX_UWORD | UREGEX_ERROR_ON_UNKNOWN_ESCAPES, status);
  check_icu(status);

  auto ok = regex_matcher.matches(status);
  check_icu(status);

  return ok;
}

}  // namespace

void check_file_exist(const std::string &file_name) {
  if (!std::filesystem::is_regular_file(file_name)) {
    klib::error("The file not exist: '{}'", file_name);
  }
}

void check_dir_exist(const std::string &dir_name) {
  if (!std::filesystem::is_directory(dir_name)) {
    klib::error("The directory not exist: '{}'", dir_name);
  }
}

void check_is_txt_file(const std::string &file_name) {
  check_file_exist(file_name);

  if (std::filesystem::path(file_name).extension() != ".txt") {
    klib::error("Need a txt file: {}", file_name);
  }
}

void remove_file_or_dir(const std::string &path) {
  if (!std::filesystem::exists(path)) {
    klib::error("The item does not exist: {}", path);
  }

  if (std::filesystem::is_regular_file(path)) {
    if (!std::filesystem::remove(path)) {
      klib::error("File deletion failed: {}", path);
    }
  } else if (std::filesystem::is_directory(path)) {
    if (std::filesystem::remove_all(path) == 0) {
      klib::error("Folder deletion failed: {}", path);
    }
  } else {
    klib::error("Not a file or folder");
  }
}

void check_is_book_id(const std::string &book_id) {
  if (!std::all_of(std::begin(book_id), std::end(book_id),
                   [](char c) { return std::isdigit(c); })) {
    klib::error("Need a book id: {}", book_id);
  }
}

std::vector<std::string> read_file_to_vec(const std::string &file_name,
                                          bool translation) {
  auto str = klib::read_file(file_name, false);

  std::vector<std::string> result;
  boost::split(result, str, boost::is_any_of("\n"), boost::token_compress_on);

  for (auto &item : result) {
    item = trans_str(item, translation);
    if (!klib::validate_utf8(item)) {
      klib::error("Invalid UTF-8: {}", item);
    }
  }

  std::erase_if(result,
                [](const std::string &line) { return std::empty(line); });

  return result;
}

void str_check(const std::string &str) {
  static std::unordered_set<char32_t> set;

  auto copy = str;
  std::erase_if(copy, [](char c) { return std::isalnum(c) || c == ' '; });

  for (auto c : klib::utf8_to_utf32(copy)) {
    if (!klib::is_chinese(c) && !is_punctuation(c)) {
      if (set.contains(c)) {
        continue;
      }

      set.insert(c);

      klib::warn("Unknown character: {} in {}",
                 klib::utf32_to_utf8(std::u32string(&c, 1)), str);
    }
  }
}

std::int32_t str_size(const std::string &str) {
  std::int32_t count = 0;

  for (auto c : klib::utf8_to_utf32(str)) {
    if (klib::is_chinese(c)) {
      ++count;
    }
  }

  return count;
}

void volume_name_check(const std::string &volume_name) {
  if (!regex_match(volume_name,
                   R"(第([一二三四五六七八九十]|[0-9]){1,3}卷 .+)")) {
    klib::warn("Irregular volume name format: {}", volume_name);
    return;
  }

  str_check(volume_name);
}

void title_check(const std::string &title) {
  if (!regex_match(title,
                   R"(第([零一二三四五六七八九十百千]|[0-9]){1,7}[章话] .+)")) {
    klib::warn("Irregular title format: {}", title);
    return;
  }

  str_check(title);
}

void push_back(std::vector<std::string> &texts, const std::string &str,
               bool connect) {
  if (std::empty(str)) {
    return;
  }

  if (std::empty(texts)) {
    texts.push_back(str);
    return;
  }

  if (texts.back().ends_with("，")) {
    if ((start_with_chinese(str) || std::isalnum(str.front())) ||
        (str.starts_with("—") || str.starts_with("“") ||
         str.starts_with("「") || str.starts_with("『") ||
         str.starts_with("《") || str.starts_with("[") ||
         str.starts_with("【") || str.starts_with("（"))) {
      texts.back().append(str);
    } else {
      klib::warn("Punctuation may be wrong: {}, previous row: {}", str,
                 texts.back());
      texts.push_back(str);
    }
  } else if (str.starts_with("！") || str.starts_with("？") ||
             str.starts_with("，") || str.starts_with("。") ||
             str.starts_with("、") || str.starts_with("”") ||
             str.starts_with("」") || str.starts_with("』") ||
             str.starts_with("》") || str.starts_with("]") ||
             str.starts_with("】") || str.starts_with("）")) {
    if (!end_with_punctuation(texts.back())) {
      texts.back().append(str);
    } else {
      if (!(str.starts_with("！") || str.starts_with("？"))) {
        klib::warn("Punctuation may be wrong: {}", str);
      }

      texts.push_back(str);
    }
  } else if (connect && std::isalpha(texts.back().back()) &&
             std::isalpha(str.front())) {
    texts.back().append(" " + str);
  } else if (connect && end_with_chinese(texts.back()) &&
             std::isalpha(str.front())) {
    texts.back().append(" " + str);
  } else if (connect && std::isalpha(texts.back().back()) &&
             start_with_chinese(str)) {
    texts.back().append(" " + str);
  } else if (connect && end_with_chinese(texts.back()) &&
             start_with_chinese(str)) {
    texts.back().append(str);
  } else {
    texts.push_back(str);
  }
}

void push_back(std::vector<std::string> &texts, const std::string &str) {
  if (auto std_str = klib::trim_copy(str); !std::empty(std_str)) {
    texts.push_back(std::move(std_str));
  }
}

void check_icu(UErrorCode status) {
  if (U_FAILURE(status)) {
    klib::error("{}", u_errorName(status));
  }
}

std::string get_login_name() {
  std::string login_name;

  std::cout << "login name: ";
  std::cin >> login_name;
  if (std::empty(login_name)) {
    klib::error("login name is empty");
  }

  return login_name;
}

std::string get_password() {
  std::string password = getpass("password: ");

  if (std::empty(password)) {
    klib::error("password is empty");
  }

  return password;
}

std::string num_to_str(std::int32_t i) {
  Expects(i > 0);

  auto str = std::to_string(i);
  if (i < 10) {
    return "00" + str;
  } else if (i < 100) {
    return "0" + str;
  } else {
    return str;
  }
}

void generate_txt(
    const std::string &book_name, const std::string &author,
    const std::vector<std::string> &description,
    const std::vector<std::pair<std::string, std::string>> &chapters) {
  std::ostringstream oss;

  oss << "[AUTHOR]" << '\n';
  oss << author << "\n\n";

  oss << "[INTRO]" << '\n';
  for (const auto &line : description) {
    oss << line << "\n";
  }
  oss << "\n";

  for (const auto &[chapter_title, content] : chapters) {
    oss << "[WEB] " << chapter_title << "\n\n";
    oss << content << "\n\n";
  }

  std::string str = oss.str();
  // '\n'
  str.pop_back();

  std::ofstream book_ofs(make_book_name_legal(book_name) + ".txt");
  book_ofs << str << std::flush;
}

void generate_txt(
    const std::string &book_name, const std::string &author,
    const std::vector<std::string> &description,
    const std::vector<std::pair<
        std::string,
        std::vector<std::tuple<std::string, std::string, std::string>>>>
        &volume_chapter) {
  std::ostringstream oss;

  oss << "[AUTHOR]" << '\n';
  oss << author << "\n\n";

  oss << "[INTRO]" << '\n';
  for (const auto &line : description) {
    oss << line << "\n";
  }
  oss << "\n";

  for (const auto &[volume_name, chapters] : volume_chapter) {
    if (std::empty(chapters)) {
      continue;
    }

    oss << "[VOLUME] " << volume_name << "\n\n";

    for (const auto &[chapter_id, chapter_title, content] : chapters) {
      oss << "[WEB] " << chapter_title << "\n\n";
      oss << content << "\n\n";
    }
  }

  std::string str = oss.str();
  // '\n'
  str.pop_back();

  std::ofstream book_ofs(make_book_name_legal(book_name) + ".txt");
  book_ofs << str << std::flush;
}

}  // namespace kepub
