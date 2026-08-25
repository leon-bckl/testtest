#pragma once

#include <algorithm>
#include <concepts>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <format>
#include <functional>
#include <iostream>
#include <memory>
#include <source_location>
#include <span>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <typeinfo>
#include <utility>
#include <variant>
#include <vector>

namespace test{

class TestFailure{
public:
	TestFailure(std::string message, std::source_location location = std::source_location::current())
		: m_message{std::move(message)}
		, m_location{location}
	{
	}

	[[nodiscard]] auto message() const -> const std::string&{ return m_message; }
	[[nodiscard]] auto location() const -> std::source_location{ return m_location; }

private:
	std::string          m_message;
	std::source_location m_location;
};

[[noreturn]]
inline void fail(std::string_view message, std::source_location location = std::source_location::current())
{
	throw TestFailure(std::string(message), location);
}

inline void check(bool condition, std::string_view message = {}, std::source_location location = std::source_location::current())
{
	if(!condition)
	{
		auto failMessage = std::string("Check failed");

		if(!message.empty())
		{
			failMessage += ": ";
			failMessage += message;
		}

		fail(failMessage, location);
	}
}

inline auto toString(std::nullptr_t) -> std::string
{
	return "nullptr";
}

inline auto toString(bool b) -> std::string
{
	return b ? "true" : "false";
}

inline auto toString(const std::type_info& typeInfo) -> std::string
{
	return std::format("(type_info){}", typeInfo.name());
}

template<typename T>
auto toString([[maybe_unused]] const T& t) -> std::string
{
	if constexpr(std::is_pointer_v<T>)
	{
		return std::format("({}*)0x{:x}",
			typeid(std::remove_pointer_t<T>).name(),
			reinterpret_cast<std::uintptr_t>(t)
		);
	}
	else
	{
		return '<' + std::string(typeid(T).name()) + '>';
	}
}

template<typename T>
requires std::integral<T> || std::floating_point<T>
auto toString(const T& value) -> std::string
{
	return std::to_string(value);
}

inline auto toString(std::string_view str) -> std::string
{
	auto result = std::string();
	result.reserve(str.size() + 2);
	result += "\"";

	for(const char c : str)
	{
		switch(c)
		{
		case '\0':
			result += "\\0";
			break;
		case '\a':
			result += "\\a";
			break;
		case '\b':
			result += "\\b";
			break;
		case '\t':
			result += "\\t";
			break;
		case '\n':
			result += "\\n";
			break;
		case '\v':
			result += "\\v";
			break;
		case '\f':
			result += "\\f";
			break;
		case '\r':
			result += "\\r";
			break;
		case '\"':
			result += "\\\"";
			break;
		case '\\':
			result += "\\\\";
			break;
		default:
			if(static_cast<unsigned char>(c) < 0x20 || static_cast<unsigned char>(c) >= 0x7F)
				result += std::format("\\u{:04x}", static_cast<unsigned int>(c));
			else
				result += c;
		}
	}

	result += '\"';

	return result;
}

inline auto toString(const std::string& str) -> std::string
{
	return toString(std::string_view(str));
}

inline auto toString(const char* str) -> std::string
{
	return toString(std::string_view(str));
}

inline auto toString(const std::filesystem::path& path) -> std::string
{
	return toString(std::string_view(path.string()));
}

template<typename T>
auto toString(const std::span<const T>& span) -> std::string
{
	auto result = std::string("{");

	const auto size = span.size();
	for(auto i = 0ul; i < size; ++i)
	{
		result += toString(span[i]);

		if(i != size - 1)
			result += ',';
	}

	result += '}';

	return result;
}

template<typename T>
requires std::is_enum_v<T>
auto toString(const T& enumValue) -> std::string
{
	return "(enum)" + std::to_string(static_cast<std::underlying_type_t<T>>(enumValue));
}

template<typename ...Args>
auto toString(const std::variant<Args...>& variantValue) -> std::string
{
	return std::visit([](const auto& value){ return "(variant)" + toString(value); }, variantValue);
}

template<typename T>
auto toString(const std::optional<T>& optionalValue) -> std::string
{
	return "(optional)" + (optionalValue.has_value() ? toString(*optionalValue) : "null");
}

template<typename A, typename B = A>
auto equalTo(const A& a, const B& b) -> bool
{
	if constexpr(std::is_integral_v<A> && std::is_integral_v<B>)
	{
		using CommonType = std::common_type_t<A, B>;
		return static_cast<CommonType>(a) == static_cast<CommonType>(b);
	}
	else
	{
		return a == b;
	}
}

template<typename A, typename B>
requires (!std::ranges::range<A> || !std::ranges::range<B>) ||
         std::convertible_to<const A, std::string_view> ||
         std::convertible_to<const A, std::filesystem::path>
void compare(A&& actual, B&& expected, std::source_location location = std::source_location::current())
{
		if(!equalTo(actual, expected))
			fail("Comparison failed - actual: " + toString(actual) + ", expected: " + toString(expected), location);
}

template<typename A, typename B>
requires std::ranges::range<A> &&
         std::ranges::range<B> &&
         (!std::convertible_to<const A, std::string_view>) &&
         (!std::convertible_to<const A, std::filesystem::path>)
void compare(A&& actual, B&& expected, std::source_location location = std::source_location::current())
{
	const auto actualSize   = std::ranges::size(actual);
	const auto expectedSize = std::ranges::size(expected);

	if(actualSize != expectedSize)
		fail(std::format("size mismatch - actual: {}, expected: {}", actualSize, expectedSize), location);

	auto aIt = std::ranges::begin(actual);
	auto bIt = std::ranges::begin(expected);

	for(std::size_t i = 0; i < actualSize; ++i, ++aIt, ++bIt)
	{
			if(!equalTo(*aIt, *bIt))
			{
				fail(std::format("Item mismatch at index {} - actual: {}, expected: {}",
				                 i, toString(*aIt), toString(*bIt)),
				     location);
			}
	}
}

template<typename Exception, typename F>
void expectException(F f, std::string_view what = {}, std::source_location location = std::source_location::current())
{
	try
	{
		f();
		fail("Expected exception but none was thrown", location);
	}
	catch(const TestFailure&)
	{
		throw;
	}
	catch(const Exception& e)
	{
		if constexpr(std::derived_from<Exception, std::exception>)
		{
			if(!what.empty())
				compare(e.what(), what, location);
		}
		else
		{
			(void)e;
			check(what.empty(), "'what' param cannot be used with exception that doesn't derive from std::exception");
		}
	}
	catch(...)
	{
		fail("Expected a different exception type", location);
	}
}

class TestResults{
public:
	TestResults() = default;

	void add(std::string_view testName, bool passed)
	{
		if(passed)
			++m_numPassed;
		else
			m_failedTestNames.push_back(testName);
	}

	auto numPassed() const -> int{ return m_numPassed; }
	auto numFailed() const -> int{ return static_cast<int>(m_failedTestNames.size()); }
	auto totalTests() const -> int{ return m_numPassed + numFailed(); }
	auto failedTestNames() const -> std::span<const std::string_view>{ return m_failedTestNames; }

private:
	int                           m_numPassed = 0;
	std::vector<std::string_view> m_failedTestNames;
};

class ResultLogger{
public:
	void logRunningTest(std::string_view testName, std::string_view testCaseName)
	{
		if(m_currentTestName != testName)
		{
			m_currentTestName = testName;
			std::cout << "################################ " << testName << " ################################\n";
		}

		std::cout << "Executing " << testName << "::" << testCaseName << std::endl;
	}

	void logFailure(std::string_view testName, std::string_view testCaseName, const TestFailure& failure)
	{
			std::cerr <<
				std::format("FAIL: {}::{} - {}:{}:{} - {}",
				            testName,
				            testCaseName,
				            failure.location().file_name(),
				            failure.location().line(),
				            failure.location().column(),
				            failure.message()) << std::endl;
	}

	void logSummary(const TestResults& results)
	{
		std::cout << "\nResults: " << results.numPassed() << " passed, " << results.numFailed() << " failed (" << results.totalTests() << " total)" << std::endl;
	}

private:
	std::string m_currentTestName;
};

class TestExecutor{
public:
	TestExecutor() = default;

	void execute(std::string_view testName, std::string_view testCaseName, std::function<void()> func, ResultLogger& logger, std::source_location location)
	{
		logger.logRunningTest(testName, testCaseName);
		auto passed = false;

		try
		{
			func();
			passed = true;
		}
		catch(const TestFailure& e)
		{
			logger.logFailure(testName, testCaseName, e);
		}
		catch(const std::exception& e)
		{
			logger.logFailure(testName, testCaseName, TestFailure(std::string("Unhandled std::exception: ") + e.what(), location));
		}
		catch(...)
		{
			logger.logFailure(testName, testCaseName, TestFailure("Unhandled unknown exception", location));
		}

		m_results.add(testName, passed);
	}

	auto results() -> const TestResults&{ return m_results; }

private:
	TestResults m_results;
};

template<typename ...Args>
struct TestCase{
	std::string                       name;
	std::tuple<std::decay_t<Args>...> args;
};

class TestSuiteInterface{
public:
	virtual ~TestSuiteInterface() = default;
	virtual void executeAll(TestExecutor& executor, ResultLogger& logger) const = 0;
	virtual void executeTestCase(TestExecutor& executor, std::string_view name, ResultLogger& logger) const = 0;
};
using TestSuitePtr = std::unique_ptr<TestSuiteInterface>;

template<typename ...Args>
class TestSuite : public TestSuiteInterface{
public:
	using TestFunc  = std::function<void(Args...)>;
	using TestCase  = test::TestCase<std::decay_t<Args>...>;

	TestSuite(std::string testName, TestFunc testFunc, std::source_location location)
		: m_testName{std::move(testName)}
		, m_testFunc{std::forward<TestFunc>(testFunc)}
		, m_location{location}
	{
	}

	TestSuite(const TestSuite&) = delete;
	TestSuite& operator=(const TestSuite&) = delete;

	void addTestCase(std::string name, Args&&... args)
	{
		m_testCases.push_back({std::move(name), decltype(TestCase::args)(std::forward<Args>(args)...)});
	}

	void addTestCases(std::initializer_list<TestCase> testCases)
	{
		m_testCases.reserve(m_testCases.size() + testCases.size());

		for(auto& t : testCases)
			m_testCases.push_back(std::move(t));
	}

	auto operator()(std::initializer_list<TestCase> testCases) -> TestSuite&
	{
		addTestCases(testCases);
		return *this;
	}

	void executeAll(TestExecutor& executor, ResultLogger& logger) const override
	{
		if(m_testCases.empty())
		{
			if constexpr(sizeof...(Args) == 0)
			{
				executor.execute(m_testName, m_testName, [this](){ m_testFunc(); }, logger, m_location);
				return;
			}
			else
			{
				throw std::logic_error("Test suite '" + m_testName + "' does not have any test cases");
			}
		}

		for(const auto& testCase : m_testCases)
		{
			executor.execute(m_testName, testCase.name,
				[this, &testCase]()
				{
					std::apply(m_testFunc, testCase.args);
				},
				logger,
				m_location);
		}
	}

	void executeTestCase(TestExecutor& executor, std::string_view name, ResultLogger& logger) const override
	{
		const auto it = std::ranges::find_if(m_testCases, [name=name](const TestCase& t){ return t.name == name; });

		if(it == m_testCases.end())
			throw std::logic_error("Test case '" + std::string(name) + "' does not exist in test suite '" + m_testName + "'");

		executor.execute(m_testName, it->name,
			[this, &testCase=*it]()
			{
				std::apply(m_testFunc, testCase.args);
			},
			logger,
			m_location);
	}

private:
	std::string           m_testName;
	TestFunc              m_testFunc;
	std::source_location  m_location;
	std::vector<TestCase> m_testCases;
};

class TestApp{
public:
	template<typename F>
	auto& addTest(std::string name, F&& testFunc, std::source_location location = std::source_location::current())
	{
		auto* testSuite = new TestSuite(std::move(name), std::function(std::forward<F>(testFunc)), location);
		auto  ptr       = TestSuitePtr(testSuite);
		m_tests.push_back(std::move(ptr));
		return *testSuite;
	}

	auto main(int argc = 0, const char* const* const argv = nullptr) -> int
	{
		try
		{
			(void)argc;
			(void)argv;

			auto executor = TestExecutor();
			auto logger   = ResultLogger();

			for(const auto& test : m_tests)
				test->executeAll(executor, logger);

			const auto& results = executor.results();

			logger.logSummary(results);

			if(results.numFailed() > 0)
				return EXIT_FAILURE;
		}
		catch(const std::exception& e)
		{
			std::cerr << "ERROR: " << e.what() << std::endl;
			return EXIT_FAILURE;
		}

		return EXIT_SUCCESS;
	}

private:
	std::vector<TestSuitePtr> m_tests;
};

} // namespace test
