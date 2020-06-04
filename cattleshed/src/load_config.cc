#include "load_config.hpp"

#include <map>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include <boost/algorithm/string/join.hpp>
#include <boost/fusion/include/io.hpp>
#include <boost/fusion/include/std_pair.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/spirit/include/phoenix.hpp>
#include <boost/spirit/include/qi.hpp>
#include <boost/spirit/include/support_istream_iterator.hpp>
#include <boost/spirit/include/support_line_pos_iterator.hpp>
#include <boost/spirit/include/support_multi_pass.hpp>
#include <boost/variant.hpp>

#include <spdlog/spdlog.h>

#include "posixapi.hpp"

namespace wandbox {
namespace cfg {
struct wandbox_cfg_tag {};
typedef boost::make_recursive_variant<
    wandbox_cfg_tag, std::string, std::vector<boost::recursive_variant_>,
    std::unordered_map<std::string, boost::recursive_variant_>, int, bool>::type
    value;
typedef std::string string;
typedef std::unordered_map<string, value> object;
typedef std::vector<value> array;

namespace qi = boost::spirit::qi;

template <typename Iter>
struct config_grammar : qi::grammar<Iter, value(), qi::space_type> {
  config_grammar() : qi::grammar<Iter, value(), qi::space_type>(top) {
    namespace phx = boost::phoenix;
    top %= (obj | arr) > qi::eoi;
    val %= obj | arr | str | qi::int_ | qi::bool_;
    pair %= str > ':' > val;
    obj %= '{' > (((pair % ',') > -qi::lit(',')) | qi::eps) > '}';
    arr %= '[' > (((val % ',') > -qi::lit(',')) | qi::eps) > ']';
    str %= qi::lexeme['\"' > *(('\\' > (qi::char_("\\\"'") |
                                        (qi::lit('t') > qi::attr('\t')) |
                                        (qi::lit('r') > qi::attr('\r')) |
                                        (qi::lit('n') > qi::attr('\n')))) |
                               (qi::char_ - '\"')) > '\"'];
    //debug(top);
    //debug(val);
    //debug(pair);
    //debug(obj);
    //debug(arr);
    //debug(str);
  }
  qi::rule<Iter, value(), qi::space_type> top;
  qi::rule<Iter, std::pair<string, value>(), qi::space_type> pair;
  qi::rule<Iter, object(), qi::space_type> obj;
  qi::rule<Iter, array(), qi::space_type> arr;
  qi::rule<Iter, value(), qi::space_type> val;
  qi::rule<Iter, string(), qi::space_type> str;
};

struct operator_output : boost::static_visitor<std::ostream&> {
  explicit operator_output(std::ostream& os, int indent)
      : os(os), indent(indent) {}
  std::ostream& operator()(const string& str) const {
    return os << '\"' << str << '\"';
  }
  std::ostream& operator()(const object& obj) const {
    if (obj.empty()) return os << "{}";
    os << "{\n";
    const operator_output print(os, indent + 1);
    for (const auto& x : obj) {
      put_indent(1);
      print(x.first) << ':';
      boost::apply_visitor(print, x.second) << ",\n";
    }
    put_indent(0);
    os << "}";
    return os;
  }
  std::ostream& operator()(const array& arr) const {
    if (arr.empty()) return os << "[]";
    os << "[\n";
    const operator_output print(os, indent + 1);
    for (const auto& x : arr) {
      put_indent(1);
      boost::apply_visitor(print, x) << ",\n";
    }
    put_indent(0);
    os << "]";
    return os;
  }
  std::ostream& operator()(const int& i) const { return os << i; }
  std::ostream& operator()(const bool& bool_) const {
    return os << (bool_ ? "true" : "false");
  }
  std::ostream& operator()(const wandbox_cfg_tag&) const { return os; }
  void put_indent(int add) const { os << std::string(indent + add, ' '); }
  std::ostream& os;
  int indent;
};

inline std::ostream& operator<<(std::ostream& os, const value& val) {
  return boost::apply_visitor(operator_output(os, 0), val);
}
}  // namespace cfg

namespace detail {
template <typename Map>
boost::optional<const typename Map::mapped_type&> find(
    const Map& m, const typename Map::key_type& k) {
  const auto ite = m.find(k);
  if (ite == m.end()) return {};
  return ite->second;
}
inline std::string get_str(const cfg::object& x, const cfg::string& key) {
  if (const auto& v = find(x, key)) return boost::get<cfg::string>(*v);
  return {};
};
inline int get_int(const cfg::object& x, const cfg::string& key) {
  if (const auto& v = find(x, key)) return boost::get<int>(*v);
  return 0;
}
inline bool get_bool(const cfg::object& x, const cfg::string& key) {
  if (const auto& v = find(x, key)) return boost::get<bool>(*v);
  return false;
}
inline std::vector<cfg::string> get_str_array(const cfg::object& x,
                                              const cfg::string& key) {
  if (const auto& v = find(x, key)) {
    if (const auto* s = boost::get<cfg::string>(&*v)) return {*s};
    std::vector<cfg::string> ret;
    for (const auto& s : boost::get<cfg::array>(*v))
      ret.emplace_back(boost::get<cfg::string>(s));
    return ret;
  }
  return {};
}
}  // namespace detail

void load_switch_single(compiler_trait& ct, const cfg::object& o) {
  using namespace detail;
  switch_trait t;
  t.group_name = t.name = get_str(o, "name");
  t.display_name = get_str(o, "display-name");
  t.runtime = get_bool(o, "runtime");
  t.insert_position = get_int(o, "insert-position");
  if (const auto v = find(o, "flags")) {
    for (auto&& e : boost::get<cfg::array>(*v)) {
      auto&& c = boost::get<cfg::object>(e);
      t.name = get_str(c, "name");
      t.flags = get_str_array(c, "values");
      if (const auto v = find(c, "display-flags"))
        t.display_flags = boost::get<cfg::string>(*v);
      else
        t.display_flags = boost::none;
      if (const auto v = find(c, "display-name"))
        t.display_name = boost::get<cfg::string>(*v);
      else
        t.display_name = t.name;
      ct.local_switches.push_back(t);
      ct.switches.push_back(t.name);
    }
  } else {
    t.flags = get_str_array(o, "values");
    if (const auto v = find(o, "display-flags"))
      t.display_flags = boost::get<cfg::string>(*v);
    else
      t.display_flags = boost::none;
    ct.local_switches.push_back(t);
    ct.switches.push_back(t.name);
  }
}

compiler_set load_compiler_trait(const cfg::value& o) {
  using namespace detail;
  compiler_set ret;
  std::unordered_map<std::string, std::vector<std::string>> inherit_map;
  std::unordered_multimap<std::string, compiler_trait> append_map;
  for (auto& x :
       boost::get<cfg::array>(boost::get<cfg::object>(o).at("compilers"))) {
    auto& y = boost::get<cfg::object>(x);
    compiler_trait t;
    t.name = get_str(y, "name");
    t.language = get_str(y, "language");
    t.compile_command = get_str_array(y, "compile-command");
    t.version_command = get_str_array(y, "version-command");
    t.run_command = get_str_array(y, "run-command");
    t.output_file = get_str(y, "output-file");
    t.display_name = get_str(y, "display-name");
    t.display_compile_command = get_str(y, "display-compile-command");
    t.jail_name = get_str(y, "jail-name");
    t.displayable = get_bool(y, "displayable");
    t.compiler_option_raw = get_bool(y, "compiler-option-raw");
    t.runtime_option_raw = get_bool(y, "runtime-option-raw");
    t.templates = get_str_array(y, "templates");
    if (const auto& v = find(y, "switches")) {
      if (const auto* s = boost::get<cfg::string>(&*v)) {
        t.switches = {*s};
      } else
        for (const auto& s : boost::get<cfg::array>(*v)) {
          if (const auto* k = boost::get<cfg::string>(&s)) {
            t.switches.emplace_back(*k);
          } else {
            load_switch_single(t, boost::get<cfg::object>(s));
          }
        }
    }
    for (auto& x : get_str_array(y, "initial-checked"))
      t.initial_checked.insert(std::move(x));
    SPDLOG_INFO("load compiler {}", t.name);
    const auto inherits = get_str_array(y, "inherits");
    if (!inherits.empty()) inherit_map[t.name] = inherits;
    const auto appendto = get_str(y, "append-to");
    if (appendto.empty())
      ret.push_back(t);
    else
      append_map.emplace(appendto, std::move(t));
  }
  while (!inherit_map.empty()) {
    const auto ite = std::find_if(
        inherit_map.begin(), inherit_map.end(),
        [&](const std::pair<const std::string, std::vector<std::string>>& p) {
          return std::all_of(
              p.second.begin(), p.second.end(), [&](const std::string& target) {
                return inherit_map.find(target) == inherit_map.end();
              });
        });
    if (ite == inherit_map.end()) break;
    const auto pos = ret.get<1>().find(ite->first);
    auto sub = *pos;
    for (const auto& target : ite->second) {
      const auto& x = *ret.get<1>().find(target);
      if (sub.language.empty()) sub.language = x.language;
      if (sub.compile_command.empty()) sub.compile_command = x.compile_command;
      if (sub.version_command.empty()) sub.version_command = x.version_command;
      if (sub.run_command.empty()) sub.run_command = x.run_command;
      if (sub.output_file.empty()) sub.output_file = x.output_file;
      if (sub.display_name.empty()) sub.display_name = x.display_name;
      if (sub.display_compile_command.empty())
        sub.display_compile_command = x.display_compile_command;
      if (sub.jail_name.empty()) sub.jail_name = x.jail_name;
      if (sub.switches.empty()) sub.switches = x.switches;
      if (sub.local_switches.get<1>().empty())
        sub.local_switches = x.local_switches;
      ret.get<1>().replace(pos, sub);
    }
    inherit_map.erase(sub.name);
  }
  for (auto&& m : append_map) {
    const auto ti = ret.get<1>().find(m.first);
    if (ti == ret.get<1>().end()) continue;
    auto t = *ti;
    const auto& s = m.second;
    t.compile_command.insert(t.compile_command.end(), s.compile_command.begin(),
                             s.compile_command.end());
    t.version_command.insert(t.version_command.end(), s.version_command.begin(),
                             s.version_command.end());
    t.run_command.insert(t.run_command.end(), s.run_command.begin(),
                         s.run_command.end());
    t.initial_checked.insert(s.initial_checked.begin(),
                             s.initial_checked.end());
    t.switches.insert(t.switches.end(), s.switches.begin(), s.switches.end());
    t.local_switches.insert(t.local_switches.end(), s.local_switches.begin(),
                            s.local_switches.end());
    ret.get<1>().replace(ti, t);
  }
  return ret;
}

system_config load_system_config(const cfg::value& values) {
  using namespace detail;
  const auto& o =
      boost::get<cfg::object>(boost::get<cfg::object>(values).at("system"));
  return {get_int(o, "listen-port"), get_int(o, "max-connections"),
          get_str(o, "basedir"), get_str(o, "storedir")};
}

std::unordered_map<std::string, jail_config> load_jail_config(
    const cfg::value& values) {
  using namespace detail;
  std::unordered_map<std::string, jail_config> ret;
  for (const auto& p :
       boost::get<cfg::object>(boost::get<cfg::object>(values).at("jail"))) {
    const auto& o = boost::get<cfg::object>(p.second);
    jail_config x;
    x.jail_command = get_str_array(o, "jail-command");
    x.program_duration = get_int(o, "program-duration");
    x.compile_time_limit = get_int(o, "compile-time-limit");
    x.kill_wait = get_int(o, "kill-wait");
    x.output_limit_kill = get_int(o, "output-limit-kill");
    x.output_limit_warn = get_int(o, "output-limit-warn");
    ret[p.first] = std::move(x);
  }
  return ret;
}

std::unordered_map<std::string, switch_trait> load_switches(
    const cfg::value& values) {
  using namespace detail;
  std::unordered_map<std::string, switch_trait> ret;
  for (const auto& a : boost::get<cfg::object>(
           boost::get<cfg::object>(values).at("switches"))) {
    const auto& s = boost::get<cfg::object>(a.second);
    switch_trait x;
    x.name = a.first;
    x.flags = get_str_array(s, "flags");
    x.display_name = get_str(s, "display-name");
    if (const auto v = find(s, "display-flags"))
      x.display_flags = boost::get<cfg::string>(*v);
    else
      x.display_flags = boost::none;
    if (const auto v = find(s, "group"))
      x.group = boost::get<cfg::string>(*v);
    else
      x.group = boost::none;
    x.runtime = get_bool(s, "runtime");
    x.insert_position = get_int(s, "insert-position");
    ret[a.first] = std::move(x);
  }
  return ret;
}

std::unordered_map<std::string, template_trait> load_templates(
    const cfg::value& values) {
  using namespace detail;
  std::unordered_map<std::string, template_trait> ret;
  for (const auto& a : boost::get<cfg::object>(
           boost::get<cfg::object>(values).at("templates"))) {
    const auto& s = boost::get<cfg::object>(a.second);
    template_trait x;
    x.name = a.first;
    x.code = get_str(s, "code");
    if (find(s, "codes")) x.codes = get_str_array(s, "codes");
    if (const auto v = find(s, "stdin")) x.stdin = boost::get<cfg::string>(*v);
    if (const auto v = find(s, "options"))
      x.options = boost::get<cfg::string>(*v);
    if (const auto v = find(s, "compiler-option-raw"))
      x.compiler_option_raw = boost::get<cfg::string>(*v);
    if (const auto v = find(s, "runtime-option-raw"))
      x.runtime_option_raw = boost::get<cfg::string>(*v);
    ret[a.first] = std::move(x);
  }
  return ret;
}

struct read_fd_iterator {
  typedef std::input_iterator_tag iterator_category;
  typedef char value_type;
  typedef std::ptrdiff_t difference_type;
  typedef const char* pointer;
  typedef const char& reference;
  explicit read_fd_iterator(int fd = -1) : buf(), off(), gone(), fd(fd) {
    fetch();
  }
  read_fd_iterator operator++(int) {
    auto tmp = *this;
    ++*this;
    return tmp;
  }
  read_fd_iterator& operator++() {
    ++off;
    ++gone;
    fetch();
    return *this;
  }
  const char& operator*() const { return buf[off]; }
  bool operator==(const read_fd_iterator& o) const {
    return (fd == -1 && o.fd == -1) || (fd == o.fd && gone == o.gone);
  }
  bool operator!=(const read_fd_iterator& o) const { return !(*this == o); }

 private:
  void fetch() {
    if (fd == -1) return;
    if (off == buf.size()) {
      buf.resize(BUFSIZ);
      const auto r = ::read(fd, buf.data(), buf.size());
      if (r == 0) {
        fd = -1;
        off = 0;
        return;
      }
      if (r < 0) throw_system_error(errno);
      buf.resize(r);
      off = 0;
    }
  }
  std::vector<char> buf;
  std::size_t off;
  std::size_t gone;
  int fd;
};

template <typename Iter>
struct parse_config_error : std::runtime_error {
  parse_config_error(const std::string& file,
                     const boost::spirit::qi::expectation_failure<Iter>& e)
      : std::runtime_error(make_what(file, e)) {}
  static std::string make_what(
      const std::string& file,
      const boost::spirit::qi::expectation_failure<Iter>& e) {
    const auto advance_for = [](Iter first, std::ptrdiff_t maxlen,
                                Iter last) -> Iter {
      for (std::ptrdiff_t n = 0; n < maxlen && first != last; ++first, ++n)
        ;
      return first;
    };
    std::stringstream ss;
    ss << "parse error in file " << file << ":" << get_line(e.first)
       << "\nwhile expecting " << e.what_ << "\nbut got "
       << std::string(e.first, advance_for(e.first, 128, e.last));
    return ss.str();
  }
};

cfg::value read_single_config_file(const std::shared_ptr<DIR>& at,
                                   const std::string& cfg) {
  SPDLOG_INFO("reading {}", cfg);
  namespace s = boost::spirit;
  namespace qi = boost::spirit::qi;
  typedef s::multi_pass<
      read_fd_iterator,
      s::iterator_policies::default_policy<
          s::iterator_policies::ref_counted, s::iterator_policies::no_check,
          s::iterator_policies::input_iterator,
          s::iterator_policies::split_std_deque>>
      functor_multi_pass_type;
  auto fd = unique_fd(::openat(dirfd_or_cwd(at), cfg.c_str(), O_RDONLY));
  if (fd.get() == -1) throw_system_error(errno);
  s::line_pos_iterator<functor_multi_pass_type> first(
      functor_multi_pass_type(read_fd_iterator(fd.get())));
  s::line_pos_iterator<functor_multi_pass_type> last;
  cfg::value o;
  try {
    qi::phrase_parse(first, last, cfg::config_grammar<decltype(first)>(),
                     qi::space, o);
  } catch (qi::expectation_failure<decltype(first)>& e) {
    throw parse_config_error<decltype(first)>(cfg, e);
  }
  return o;
}

std::vector<cfg::value> read_config_file(const std::shared_ptr<DIR>& at,
                                         const std::string& name) {
  try {
    std::vector<cfg::value> ret;
    const auto dir = opendirat(at, name);
    std::vector<std::string> files;
    for (auto ent = readdir(dir.get()); ent; ent = readdir(dir.get())) {
      if (::strcmp(ent->d_name, ".") == 0 || ::strcmp(ent->d_name, "..") == 0)
        continue;
      files.emplace_back(ent->d_name);
    }
    std::sort(files.begin(), files.end());
    for (const auto& f : files) {
      const auto x = read_config_file(dir, f.c_str());
      ret.insert(ret.end(), x.begin(), x.end());
    }
    return ret;
  } catch (std::system_error& e) {
    if (e.code().value() != ENOTDIR) throw;
    return {read_single_config_file(at, name)};
  }
}

struct merge_cfgs_visitor {
  typedef cfg::value result_type;
  cfg::value operator()(std::vector<cfg::value> a,
                        const std::vector<cfg::value>& b) const {
    a.insert(a.end(), b.begin(), b.end());
    return a;
  }
  cfg::value operator()(
      std::unordered_map<std::string, cfg::value> a,
      const std::unordered_map<std::string, cfg::value>& b) const {
    for (const auto& kv : b) {
      a[kv.first] = boost::apply_visitor(*this, a[kv.first], kv.second);
    }
    return a;
  }
  template <typename T, typename U>
  cfg::value operator()(const T&, const U& b) const {
    return b;
  }
  template <typename T>
  cfg::value operator()(const T& a, cfg::wandbox_cfg_tag) const {
    return a;
  }
  template <typename T>
  cfg::value operator()(cfg::wandbox_cfg_tag, const T& b) const {
    return b;
  }
  cfg::value operator()(cfg::wandbox_cfg_tag, cfg::wandbox_cfg_tag) const {
    return {};
  }
};

cfg::value merge_cfgs(const std::vector<cfg::value>& cfgs) {
  cfg::value ret;
  for (const auto& c : cfgs) {
    ret = boost::apply_visitor(merge_cfgs_visitor(), ret, c);
  }
  return ret;
}

server_config load_config(const std::vector<std::string>& cfgs) {
  std::vector<cfg::value> os;
  for (const auto& c : cfgs) {
    const auto x = read_config_file(nullptr, c);
    os.insert(os.end(), x.begin(), x.end());
  }
  const auto o = merge_cfgs(os);
  return {load_system_config(o), load_jail_config(o), load_compiler_trait(o),
          load_switches(o), load_templates(o)};
}

template <typename Iter>
struct char_escaping_iterator
    : std::iterator<std::input_iterator_tag,
                    typename std::iterator_traits<Iter>::value_type> {
  typedef std::input_iterator_tag iterator_category;
  typedef typename std::iterator_traits<Iter>::value_type value_type;
  typedef typename std::iterator_traits<Iter>::difference_type difference_type;
  typedef const value_type& reference;
  typedef const value_type* pointer;
  char_escaping_iterator(Iter base) : base_(base), escaping(false) {}
  Iter base() const { return base_; }
  reference operator*() const {
    if (escaping) {
      switch (*base_) {
        case '\\':
          return escape_backslash;
        case '\"':
          return escape_dquot;
        case '\t':
          return escape_tab;
        case '\r':
          return escape_cr;
        case '\n':
          return escape_nl;
      }
    } else {
      switch (*base_) {
        case '\\':
        case '\"':
        case '\t':
        case '\r':
        case '\n':
          return escape_backslash;
      }
    }
    return *base_;
  }
  char_escaping_iterator& operator++() {
    if (escaping) {
      ++base_;
      escaping = false;
    } else {
      switch (*base_) {
        case '\\':
        case '\"':
        case '\t':
        case '\r':
        case '\n':
          escaping = true;
          break;
        default:
          ++base_;
      }
    }
    return *this;
  }
  char_escaping_iterator operator++(int) {
    auto tmp(*this);
    ++*this;
    return tmp;
  }
  bool operator==(const char_escaping_iterator& rhs) const {
    return base_ == rhs.base_ && escaping == rhs.escaping;
  }
  bool operator!=(const char_escaping_iterator& rhs) const {
    return !(*this == rhs);
  }
  typename std::enable_if<
      std::is_convertible<decltype(std::declval<Iter>() < std::declval<Iter>()),
                          bool>::value,
      bool>::type
  operator<(const char_escaping_iterator& rhs) const {
    return (base_ < rhs.base_) ||
           (base_ == rhs.base_ && !escaping && rhs.escaping);
  }
  typename std::enable_if<
      std::is_convertible<decltype(std::declval<Iter>() < std::declval<Iter>()),
                          bool>::value,
      bool>::type
  operator>(const char_escaping_iterator& rhs) const {
    return rhs < *this;
  }
  typename std::enable_if<
      std::is_convertible<decltype(std::declval<Iter>() < std::declval<Iter>()),
                          bool>::value,
      bool>::type
  operator<=(const char_escaping_iterator& rhs) const {
    return *this < rhs || *this == rhs;
  }
  typename std::enable_if<
      std::is_convertible<decltype(std::declval<Iter>() < std::declval<Iter>()),
                          bool>::value,
      bool>::type
  operator>=(const char_escaping_iterator& rhs) const {
    return rhs < *this || rhs == *this;
  }

 private:
  Iter base_;
  bool escaping;
  static const char escape_backslash;
  static const char escape_dquot;
  static const char escape_tab;
  static const char escape_cr;
  static const char escape_nl;
};
template <typename Iter>
const char char_escaping_iterator<Iter>::escape_backslash = '\\';
template <typename Iter>
const char char_escaping_iterator<Iter>::escape_dquot = '\"';
template <typename Iter>
const char char_escaping_iterator<Iter>::escape_tab = 't';
template <typename Iter>
const char char_escaping_iterator<Iter>::escape_cr = 'r';
template <typename Iter>
const char char_escaping_iterator<Iter>::escape_nl = 'n';
template <typename Iter>
char_escaping_iterator<Iter> char_escaper(Iter ite) {
  return {ite};
}
std::string json_stringize(const std::string& in) {
  return std::string(char_escaper(in.begin()), char_escaper(in.end()));
}

template <class T>
struct is_pair : std::integral_constant<bool, false> {};
template <class T, class U>
struct is_pair<std::pair<T, U>> : std::integral_constant<bool, true> {};

template <class T>
std::true_type is_map_like_(
    int, typename std::enable_if<is_pair<typename std::remove_reference<
             decltype(*std::declval<typename std::remove_reference<T>::type>()
                           .begin())>::type>::value>::type* = 0);
template <class T>
std::false_type is_map_like_(...);
template <class T>
struct is_map_like
    : decltype(is_map_like_<typename std::remove_cv<
                   typename std::remove_reference<T>::type>::type>(0)) {};

template <std::size_t N>
void is_string_literal_check(const char (&)[N]);

template <class T>
std::true_type is_string_literal_(
    int,
    typename std::enable_if<decltype(is_string_literal_check(std::declval<T>()),
                                     std::true_type())::value>::type* = 0);
template <class T>
std::false_type is_string_literal_(...);

template <class T>
struct is_string_literal : decltype(is_string_literal_<T>(0)) {};

template <class T>
struct is_string_ : std::integral_constant<bool, is_string_literal<T>::value> {
};
template <>
struct is_string_<std::string> : std::integral_constant<bool, true> {};
template <>
struct is_string_<const char*> : std::integral_constant<bool, true> {};
template <class T>
struct is_string : is_string_<typename std::remove_cv<
                       typename std::remove_reference<T>::type>::type> {};

template <class T>
std::true_type is_vector_like_(
    int,
    typename std::enable_if<!is_map_like<T>::value && !is_string<T>::value &&
                            decltype(*std::declval<T>().begin(),
                                     std::true_type())::value>::type* = 0);
template <class T>
std::false_type is_vector_like_(...);
template <class T>
struct is_vector_like
    : decltype(is_vector_like_<typename std::remove_cv<
                   typename std::remove_reference<T>::type>::type>(0)) {};

enum class json_value_type {
  object,
  array,
  string,
  number,
};
class json_object
    : std::integral_constant<json_value_type, json_value_type::object> {};
class json_array
    : std::integral_constant<json_value_type, json_value_type::array> {};
class json_string
    : std::integral_constant<json_value_type, json_value_type::string> {};
class json_number
    : std::integral_constant<json_value_type, json_value_type::number> {};

template <class T, bool IsObject, bool IsArray, bool IsString, bool IsNumber>
struct json_value_t_;
template <class T>
struct json_value_t_<T, true, false, false, false> : json_object {};
template <class T>
struct json_value_t_<T, false, true, false, false> : json_array {};
template <class T>
struct json_value_t_<T, false, false, true, false> : json_string {};
template <class T>
struct json_value_t_<T, false, false, false, true> : json_number {};

template <class T>
struct json_value_t
    : json_value_t_<T, is_map_like<T>::value, is_vector_like<T>::value,
                    is_string<T>::value, std::is_arithmetic<T>::value> {};

template <class Value>
std::string json_serialize(Value&& value);

template <class Value>
std::string json_serialize_(Value&& value, const json_object&) {
  std::vector<std::string> xs;
  for (auto&& v : value) {
    xs.push_back("\"" + json_stringize(v.first) +
                 "\":" + json_serialize(v.second));
  }
  return "{" + boost::algorithm::join(xs, ",") + "}";
}
template <class Value>
std::string json_serialize_(Value&& value, const json_array&) {
  std::vector<std::string> xs;
  for (auto&& v : value) {
    xs.push_back(json_serialize(v));
  }
  return "[" + boost::algorithm::join(xs, ",") + "]";
}
template <class Value>
std::string json_serialize_(Value&& value, const json_string&) {
  return "\"" + json_stringize(value) + "\"";
}
template <class Value>
std::string json_serialize_(Value&& value, const json_number&) {
  return std::to_string(value);
}

template <class Value>
std::string json_serialize(Value&& value) {
  auto x = json_value_t<Value>();
  return json_serialize_(std::forward<Value>(value), x);
}

std::string generate_displaying_compiler_config(
    const compiler_trait& compiler, const std::string& version,
    const std::unordered_map<std::string, switch_trait>& switches) {
  std::vector<std::string> swlist;
  {
    std::unordered_set<std::string> used;
    {
      for (const auto& sw : compiler.local_switches.get<1>()) {
        auto rng = compiler.local_switches.get<2>().equal_range(sw.group_name);
        if (rng.first->name != sw.name) continue;
        if (std::next(rng.first) != rng.second) {
          std::vector<std::string> sel;
          std::string def;
          for (; rng.first != rng.second; ++rng.first) {
            auto&& sw = *rng.first;
            sel.emplace_back(
                "{"
                "\"name\":\"" +
                json_stringize(sw.name) +
                "\","
                "\"display-name\":\"" +
                json_stringize(sw.display_name) +
                "\","
                "\"display-flags\":\"" +
                json_stringize((sw.display_flags
                                    ? *sw.display_flags
                                    : boost::algorithm::join(sw.flags, " "))) +
                "\""
                "}");
            if (compiler.initial_checked.count(sw.name) != 0) def = sw.name;
          }
          swlist.emplace_back(
              "{"
              "\"type\":\"select\","
              "\"default\":\"" +
              json_stringize(def) +
              "\","
              "\"options\":[" +
              boost::algorithm::join(sel, ",") +
              "]"
              "}");
        } else {
          auto&& sw = *rng.first;
          swlist.emplace_back(
              "{"
              "\"name\":\"" +
              json_stringize(sw.name) +
              "\","
              "\"type\":\"single\","
              "\"display-name\":\"" +
              json_stringize(sw.display_name) +
              "\","
              "\"display-flags\":\"" +
              json_stringize((sw.display_flags
                                  ? *sw.display_flags
                                  : boost::algorithm::join(sw.flags, " "))) +
              "\","
              "\"default\":" +
              (compiler.initial_checked.count(sw.name) != 0 ? "true"
                                                            : "false") +
              "}");
        }
      }
    }
    for (const auto& swname : compiler.switches) {
      if (used.count(swname) != 0) continue;
      if (compiler.local_switches.get<1>().count(swname)) continue;
      const auto ite = switches.find(swname);
      if (ite == switches.end()) continue;
      const auto& sw = ite->second;
      const auto& group = sw.group;
      if (!group) {
        used.insert(swname);
        swlist.emplace_back(
            "{"
            "\"name\":\"" +
            json_stringize(sw.name) +
            "\","
            "\"type\":\"single\","
            "\"display-name\":\"" +
            json_stringize(sw.display_name) +
            "\","
            "\"display-flags\":\"" +
            json_stringize((sw.display_flags
                                ? *sw.display_flags
                                : boost::algorithm::join(sw.flags, " "))) +
            "\","
            "\"default\":" +
            (compiler.initial_checked.count(sw.name) != 0 ? "true" : "false") +
            "}");
      } else {
        std::unordered_set<std::string> groups;
        for (const auto& swname : compiler.switches) {
          const auto& sw = switches.at(swname);
          if (sw.group && *sw.group == *group) {
            groups.insert(sw.name);
          }
        }
        std::vector<std::string> sel;
        std::string def = swname;
        for (const auto& swname : compiler.switches) {
          const auto& sw = switches.at(swname);
          if (groups.count(swname) == 0) continue;
          sel.emplace_back(
              "{"
              "\"name\":\"" +
              json_stringize(sw.name) +
              "\","
              "\"display-name\":\"" +
              json_stringize(sw.display_name) +
              "\","
              "\"display-flags\":\"" +
              json_stringize((sw.display_flags
                                  ? *sw.display_flags
                                  : boost::algorithm::join(sw.flags, " "))) +
              "\""
              "}");
          if (compiler.initial_checked.count(sw.name) != 0) def = swname;
        }
        swlist.emplace_back(
            "{"
            "\"name\":\"" +
            *group +
            "\","
            "\"type\":\"select\","
            "\"default\":\"" +
            json_stringize(def) +
            "\","
            "\"options\":[" +
            boost::algorithm::join(sel, ",") +
            "]"
            "}");
        used.insert(groups.begin(), groups.end());
      }
    }
  }
  std::vector<std::string> templates;
  for (const auto& tmpl : compiler.templates) {
    templates.emplace_back("\"" + json_stringize(tmpl) + "\"");
  }
  return "{"
         "\"name\":\"" +
         json_stringize(compiler.name) +
         "\","
         "\"language\":\"" +
         json_stringize(compiler.language) +
         "\","
         "\"display-name\":\"" +
         json_stringize(compiler.display_name) +
         "\","
         "\"version\":\"" +
         json_stringize(version) +
         "\","
         "\"display-compile-command\":\"" +
         json_stringize(compiler.display_compile_command) +
         "\","
         "\"compiler-option-raw\":" +
         (compiler.compiler_option_raw ? "true" : "false") +
         ","
         "\"runtime-option-raw\":" +
         (compiler.runtime_option_raw ? "true" : "false") +
         ","
         "\"switches\":[" +
         boost::algorithm::join(swlist, ",") + "]" +
         ","
         "\"templates\":[" +
         boost::algorithm::join(templates, ",") +
         "]"
         "}";
}

std::string generate_templates(
    const std::unordered_map<std::string, template_trait>& templates) {
  std::vector<std::string> x;
  for (const auto& kv : templates) {
    const auto& t = kv.second;
    std::vector<std::string> y;
    y.push_back(json_serialize("name") + ":" + json_serialize(t.name));
    y.push_back(json_serialize("code") + ":" + json_serialize(t.code));
    if (t.codes)
      y.push_back(json_serialize("codes") + ":" + json_serialize(*t.codes));
    if (t.stdin)
      y.push_back(json_serialize("stdin") + ":" + json_serialize(*t.stdin));
    if (t.options)
      y.push_back(json_serialize("options") + ":" + json_serialize(*t.options));
    if (t.compiler_option_raw)
      y.push_back(json_serialize("compiler_option_raw") + ":" +
                  json_serialize(*t.compiler_option_raw));
    if (t.runtime_option_raw)
      y.push_back(json_serialize("runtime_option_raw") + ":" +
                  json_serialize(*t.runtime_option_raw));
    x.push_back(json_serialize(kv.first) + ":{" +
                boost::algorithm::join(y, ",") + "}");
  }
  return "{" + boost::algorithm::join(x, ",") + "}";
}
}  // namespace wandbox
