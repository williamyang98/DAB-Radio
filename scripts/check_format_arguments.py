from collections import namedtuple
import argparse
import colorlog
import logging
import os
import re
import sys

logger = logging.getLogger("main")
handler = colorlog.StreamHandler()
handler.setFormatter(colorlog.ColoredFormatter('%(log_color)s[%(levelname)s] [%(name)s] %(message)s'))
logger.addHandler(handler)

# location of token in file
class ParseSpan:
    def __init__(self, start_line, start_col, end_line, end_col):
        self.start_line = start_line
        self.start_col = start_col
        self.end_line = end_line
        self.end_col = end_col

    def __str__(self):
        return f"span({self.start_line}/{self.start_col}-{self.end_line}/{self.end_col})"

# unhandled errors while parsing
# these should be caught by the compiler instead
class ParseBadArgumentIndexFormat(Exception):
    def __init__(self, arg_str, arg_span, bad_index):
        self.arg_str = arg_str
        self.arg_span = arg_span
        self.bad_index = bad_index
        super().__init__(self.__str__())

    def __str__(self):
        return f"Argument '{self.arg_str}' at {self.arg_span}' has a badly formatted index {self.bad_index}'"

class ParseBadArgumentIndexValue(Exception):
    def __init__(self, fmt_args, arg):
        self.fmt_args = fmt_args
        self.arg = arg
        super().__init__(self.__str__())

    def __str__(self):
        return f"Argument '{self.arg.string}' at {self.arg.span} has index={self.arg.index} which exceeds bounds=[0,{self.fmt_args.total_required_values-1}] in format_string={self.fmt_args.string} at {self.fmt_args.span}"

# argument string in format string
class ParseArgument:
    def __init__(self, string, span):
        self.string = string
        self.span = span
        self.index = None
        self.format = None
 
        string = string.strip("{}")
        parts = string.split(":")
        if len(parts) == 2:
            index, format = parts
            index = index.strip()
            format = format.strip()
            if len(index) > 0:
                try:
                    self.index = int(index)
                except ValueError as ex:
                    raise ParseBadArgumentIndexFormat(self.string, self.span, index)
            if len(format) > 0:
                self.format = format
        else:
            if len(string) > 0:
                try:
                    self.index = int(string)
                except ValueError:
                    self.format = string

    def __str__(self):
        points = [f"'{self.string}'", str(self.span)]
        if self.index != None:
            points.append(f"index={self.index}")
        if self.format != None:
            points.append(f"format={self.format}")
        return f"arg({', '.join(points)})"

# value provided to format call
class ParseValue:
    def __init__(self, string, span):
        self.string = string
        self.span = span

    def __str__(self):
        return f"value('{self.string}', {self.span})"

# structure of format string arguments
class ParseArguments:
    def __init__(self, string, span, args):
        self.string = string.rstrip(',')
        self.span = span
        self.args = args
        self.total_positional_args = 0
        self.total_indexed_args = 0

        for arg in args:
            if arg.index != None:
                self.total_indexed_args += 1
            else:
                self.total_positional_args += 1
        self.total_required_values = self.total_positional_args
        if self.total_indexed_args > 0:
            max_index_arg = max((arg.index for arg in args if arg.index != None))
            self.total_required_values = max(self.total_positional_args, max_index_arg+1)

    def validate(self):
        for arg in self.args:
            if arg.index == None: continue
            if arg.index < 0 or arg.index >= self.total_required_values:
                raise ParseBadArgumentIndexValue(self, arg)

    def __str__(self):
        return f"args('{self.string}', {self.span}, [{', '.join(map(str,self.args))}])"

# format call result
class ParseResult:
    def __init__(self, args, values=None, is_missing_brace=True):
        self.lines = []
        self.args = args
        self.values = values if values != None else []
        self.is_missing_brace = is_missing_brace

    @property
    def is_valid(self):
        if self.args.total_required_values != len(self.values): return False 
        if self.is_missing_brace: return False
        return True

    def __str__(self):
        return f"result({self.args}, values=[{', '.join(map(str,self.values))}], brace={not self.is_missing_brace})"

# TODO: not a particularly thorough parser but good enough to check for most cases
#       ideally this should be done by the compiler but fmtlib/c++ doesn't make this easy
def read_lines(lines):
    results = []
    curr_result = None
    bracket_count = 0
    bracket_stack = []

    # format string that is enclosed by double quotes with an optional string concatenation at the end
    REGEX_FORMAT_STRING = re.compile(r'"(?:[^{}"]*\w*{[^{}"]*})+[^{}"]*"(?:[^,]*),')
    REGEX_FORMAT_ARG = re.compile(r"({[^{}]*})")

    for line_number, line in enumerate(lines):
        if len(line) == 0: return
        if line.lstrip().startswith("//"): continue # skip commented out files

        curr_col = 0
        token_start = 0

        if curr_result:
            curr_result.lines.append(line)

        while curr_col < len(line):
            if curr_result is None:
                # find format string itself
                format_string_match = next(REGEX_FORMAT_STRING.finditer(line[curr_col:]), None)
                if format_string_match:
                    format_string = format_string_match.group(0)
                    format_string_span = ParseSpan(
                        line_number, curr_col + format_string_match.start(0),
                        line_number, curr_col + format_string_match.end(0),
                    )
                    arg_matches = list(REGEX_FORMAT_ARG.finditer(format_string))
                    args_list = []
                    for arg_match in arg_matches:
                        arg_string = arg_match.group(0)
                        arg_span = ParseSpan(
                            line_number, curr_col + format_string_match.start(0) + arg_match.start(0),
                            line_number, curr_col + format_string_match.start(0) + arg_match.end(0),
                        )
                        arg = ParseArgument(arg_string, arg_span)
                        args_list.append(arg)
                    args = ParseArguments(format_string, format_string_span, args_list)
                    args.validate()
                    curr_result = ParseResult(args)
                    curr_result.lines.append(line)
                    curr_col += format_string_match.end(0)
                    token_start = curr_col
                    bracket_stack.append("(") # assume that format string was preceeded by (
                else:
                    curr_col = len(line)
            else:
                # find values provided to format string
                next_token = next(((i,c) for i,c in enumerate(line[curr_col:]) if c in (",", "(", ")", "{", "}", "\n")), None)
                if next_token != None:
                    index_end, stop_token = next_token
                else: 
                    # end of file
                    index_end, stop_token = len(line)-curr_col, None

                # handle bracket scopes
                if stop_token == "(" or stop_token == "{":
                    bracket_stack.append(stop_token) 
                    curr_col += (index_end+1)
                    continue
                if stop_token == "}":
                    assert bracket_stack[-1] == "{"
                    bracket_stack.pop()
                    curr_col += (index_end+1)
                    continue
                if stop_token == ")":
                    assert bracket_stack[-1] == "("
                    bracket_stack.pop()
                    # fall through if format call is completely finished
                    if len(bracket_stack) > 0:
                        curr_col += (index_end+1)
                        continue

                # currently inside another bracket scope which doesn't belong to format call
                # ignore any commas in here as they are parameters to a value
                if stop_token == "," and len(bracket_stack) > 1:
                    curr_col += (index_end+1)
                    continue

                # terminate value at comma, newline, end of string or closing bracket
                value_string = line[token_start:curr_col+index_end]
                value_string_lstrip = value_string.lstrip(" ")
                total_lstrip = len(value_string) - len(value_string_lstrip)
                value_string_rstrip = value_string_lstrip.rstrip(" ,")
                total_rstrip = len(value_string_lstrip) - len(value_string_rstrip)
                value_string = value_string_rstrip
                if len(value_string) > 0:
                    value_span = ParseSpan(
                        line_number, token_start+total_lstrip,
                        line_number, curr_col+index_end-total_rstrip,
                    )
                    value = ParseValue(value_string, value_span)
                    curr_result.values.append(value)
                curr_col += (index_end+1)
                token_start = curr_col

                # enclosing bracket was provided, format call is complete
                if len(bracket_stack) == 0:
                    curr_result.is_missing_brace = False
                    results.append(curr_result)
                    curr_result = None

    # dangling result is probably result of missing closing bracket ')'
    if curr_result != None:
        results.append(curr_result)
        curr_result = None

    return results

def analyse_file(filepath):
    logger.info(f"Analysing file: {filepath}")
    with open(filepath, "r", encoding="utf8") as fp:
        results = read_lines(fp.readlines())
    if len(results) > 0:
        logger.info(f"Found {len(results)} format strings")
    else:
        logger.debug(f"Found {len(results)} format strings")

    total_errors = 0
    total_successes = 0
    for index, result in enumerate(results):
        if not result.is_valid:
            logger.error(f"Found bad format string at {filepath} @ {result.args.span.start_line}/{result.args.span.start_col}")
            start_line = result.args.span.start_line
            for row, line in enumerate(result.lines):
                logger.error(f'[{start_line+row:4d}]: {line.rstrip("\n")}')
            logger.error(f"format_string={result.args.string}")
            logger.error(f"total_arguments={result.args.total_required_values}")
            logger.error(f"total_values={len(result.values)}")
            logger.error(f"is_enclosed={not result.is_missing_brace}")
            logger.error(f"arguments=[{', '.join((arg.string for arg in result.args.args))}]")
            logger.error(f"values=[{', '.join((value.string for value in result.values))}]")
            total_errors += 1
        else:
            logger.debug(f"Found good format string at {filepath} @ {result.args.span.start_line}/{result.args.span.start_col}")
            start_line = result.args.span.start_line
            for row, line in enumerate(result.lines):
                logger.debug(f'[{start_line+row:4d}]: {line.rstrip("\n")}')
            logger.debug(f"format_string={result.args.string}")
            logger.debug(f"total_arguments={result.args.total_required_values}")
            logger.debug(f"total_values={len(result.values)}")
            logger.debug(f"is_enclosed={not result.is_missing_brace}")
            logger.debug(f"arguments=[{', '.join((arg.string for arg in result.args.args))}]")
            logger.debug(f"values=[{', '.join((value.string for value in result.values))}]")
            total_successes += 1

    return total_errors, total_successes

def self_test():
    TestResult = namedtuple("TestResult", ["total_args", "total_values"])
    tests = []
    tests.append((
        "simple",
        'auto dir = fmt::format("{}/{}", args.scraper_output, channel_name);', 
        [TestResult(2,2)],
    ))
    tests.append((
        "format string with brackets and commas",
        'auto dir = fmt::format("[({}), ({})]", args.scraper_output, channel_name);', 
        [TestResult(2,2)],
    ))
    tests.append((
        "value comes from function call",
        'auto window_label = fmt::format("Simple View ({})###simple_view", instance->get_name());', 
        [TestResult(1,1)],
    ))
    tests.append((
        "value comes from complex call",
        'auto window_label = fmt::format("arg={}", std::string_view{buf.get_data(), decoder.get_max_size() - decoder.get_remaining_bytes()});',
        [TestResult(1,1)],
    ))
    tests.append((
        "format call occurs inside various scopes",
        'if (status < 0) { m_error_list.push_back(fmt::format("Failed to reset buffer ({})", status)); }',
        [TestResult(1,1)],
    ))
    tests.append((
        "argument have formatters",
        'LOG_MESSAGE("Progress partial data group {}/{:02}/{:>4}", value_0, value_1, value_2);',
        [TestResult(3,3)],
    ))
    tests.append((
        "format call spread over multiple lines",
        'LOG_MESSAGE("[header-ext] type=utc_time valid={} utc={} date={:02}/{:02}/{:04} time={:02}:{:02}:{:02}.{:03}",\n'
        'validity_flag, UTC_flag,\n'
        'day, month, year, hours, \n'
        'minutes, seconds, milliseconds);',
        [TestResult(9,9)],
    ))
    tests.append((
        "arguments have specified indices",
        'auto dir = fmt::format("{0}/{1}", arg_0, arg_1);',
        [TestResult(2,2)],
    ))
    tests.append((
        "arguments have specified indices and formatters",
        'auto dir = fmt::format("{0:2d}/{1:.3f}", args.scraper_output, channel_name);',
        [TestResult(2,2)],
    ))
    tests.append((
        "string concatenation in format string",
        'auto dir = fmt::format("{}/{}" MY_CONCATENATED_STRING, args.scraper_output, channel_name);',
        [TestResult(2,2)],
    ))
    tests.append((
        "argument with index has gap",
        'auto dir = fmt::format("{0}/{2}", arg_0, arg_1, arg_2);',
        [TestResult(3,3)],
    ))
    tests.append((
        "argument with index has gap and insufficient elements provided",
        'auto dir = fmt::format("{0}/{2}", arg_0, arg_1);',
        [TestResult(3,2)],
    ))
    tests.append((
        "class constructor",
        'static GlobalContext global_context {}', 
        [],
    ))
    tests.append((
        "class method",
        'virtual ~InputBuffer() {}', 
        [],
    ))
    tests.append((
        "dangling braces",
        '{}',
        [],
    ))
    tests.append((
        "combined super test",
        '\n'.join((lines for _, lines, _ in tests)),
        sum((results for _, _, results in tests), []),
    ))

    total_fails = 0
    for i, (name, lines, results_expected) in enumerate(tests):
        logger.info(f"Running test {i}: '{name}'")
        results_given = read_lines(lines.split("\n"))
        for result_index, result in enumerate(results_given):
            logger.debug(f"Found good format string {result_index} at {result.args.span.start_line}/{result.args.span.start_col}")
            start_line = result.args.span.start_line
            body_annotated = "\n".join((
                f'[{start_line+row:4d}]: {line.rstrip("\n")}' 
                for row, line in enumerate(result.lines)
            ))
            logger.debug(f"\n{body_annotated}")
            logger.debug(f"format_string={result.args.string}")
            logger.debug(f"total_arguments={result.args.total_required_values}")
            logger.debug(f"total_values={len(result.values)}")
            logger.debug(f"is_enclosed={not result.is_missing_brace}")
            logger.debug(f"arguments=[{', '.join((arg.string for arg in result.args.args))}]")
            logger.debug(f"values=[{', '.join((value.string for value in result.values))}]")

        is_failed = False
        if len(results_expected) != len(results_given):
            logger.error(f"Mismatching number of results. expected={len(results_expected)} given={len(results_given)}")
            is_failed = True
        for result_expected, result_given in zip(results_expected, results_given):
            if result_expected.total_args != result_given.args.total_required_values:
                logger.error(f"Mismatching number of arguments. expected={result_expected.total_args} given={result_given.args.total_required_values}")
                is_failed = True
            if result_expected.total_values != len(result_given.values):
                logger.error(f"Mismatching number of values. expected={result_expected.total_values} given={len(result_given.values)}")
                is_failed = True
            if is_failed:
                total_fails += 1
    return (total_fails, len(tests))

def main():
    log_levels = ['debug', 'info', 'warning', 'error', 'critical']
    parser = argparse.ArgumentParser()
    parser.add_argument("--self-test", action="store_true", help="Self test the script")
    parser.add_argument("--log", choices=log_levels, type=str, default="info", help="Set logging level")
    args = parser.parse_args()

    logger.setLevel(args.log.upper())

    if args.self_test:
        total_fail, total_tests = self_test()
        if total_fail > 0:
            logger.error(f"Failed self test with {total_fail}/{total_tests} tests failing")
            sys.exit(1)
        else:
            logger.info(f"Passed self test with {total_fail}/{total_tests} tests failing")
            sys.exit(0)

    DIRS = ["examples", "src"]
    EXTENSIONS = set(["cpp", "c", "h", "hpp", "cxx", "hxx", "c++", "h++"])
    files = []

    for dir in DIRS:
        for root, sub_dirs, filenames in os.walk(dir):
            for filename in filenames:
                parts = filename.split(".")
                if len(parts) <= 1: continue
                ext = parts[-1]
                ext = ext.strip()
                if not ext in EXTENSIONS: continue
                filepath = os.path.join(root, filename) 
                files.append(filepath)
    files = sorted(files)

    if len(files) == 0:
        logger.warning(f"No files to analyse (did you make sure you are in the root directory?)")
        sys.exit(1)

    logger.info(f"Analysing {len(files)} files")
    net_total_errors = 0
    net_total_successes = 0
    for filepath in files:
        total_errors, total_successes = analyse_file(filepath)
        net_total_errors += total_errors
        net_total_successes += total_successes
    net_total = net_total_errors + net_total_successes

    if net_total_errors > 0:
        logger.error(f"{net_total_errors}/{net_total} format string calls were improperly formed")
        sys.exit(1)
    else:
        logger.info(f"{net_total_errors}/{net_total} format string calls were improperly formed")
        sys.exit(0)

if __name__ == "__main__":
    main()
