#pragma once

#include <algorithm>
#include <concepts>
#include <cstdlib>
#include <exception>
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

inline void check(bool condition, std::string_view message = "Check failed", std::source_location location = std::source_location::current())
{
	if(!condition)
		fail(message, location);
}

template<typename T>
auto toString(const T&) -> std::string
{
	return '<' + std::string(typeid(T).name()) + '>';
}

template<typename T>
requires std::integral<T> || std::floating_point<T>
auto toString(const T& value) -> std::string
{
	return std::to_string(value);
}

inline auto toString(std::string_view str) -> std::string
{
	return '"' + std::string(str) + '"';
}

inline auto toString(const std::string& str) -> std::string
{
	return toString(std::string_view(str));
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

template<typename A, typename B = A>
struct Comparator{
	auto operator()(const A& a, const B& b) const -> bool
	{
		return a == b;
	}
};

template<typename A, typename B, typename Comp = Comparator<A, B>>
requires (!std::ranges::range<A> || !std::ranges::range<B>) || std::convertible_to<const A, std::string_view>
void compare(A&& actual, B&& expected, Comp&& comp = Comp{}, std::source_location location = std::source_location::current())
{
	check(
		comp(actual, expected),
		"Comparison failed - actual: " + toString(actual) + ", expected: " + toString(expected),
		location);
}

template<typename A, typename B, typename Comp = Comparator<std::ranges::range_value_t<A>, std::ranges::range_value_t<B>>>
requires std::ranges::range<A> && std::ranges::range<B> && (!std::convertible_to<const A, std::string_view>)
void compare(A&& actual, B&& expected, Comp&& comp = Comp{}, std::source_location location = std::source_location::current())
{
	const auto actualSize   = std::ranges::size(actual);
	const auto expectedSize = std::ranges::size(expected);

	check(actualSize == expectedSize,
	      std::format("size mismatch - actual: {}, expected: {}", actualSize, expectedSize),
	      location);

	auto aIt = std::ranges::begin(actual);
	auto bIt = std::ranges::begin(expected);

	for(std::size_t i = 0; i < actualSize; ++i, ++aIt, ++bIt)
	{
		check(
			comp(*aIt, *bIt),
			"Item mismatch at index " + std::to_string(i) +
				" - actual: " + toString(actual) + ", expected: " + toString(expected),
			location);
	}
}

template<typename Exception, typename F>
void expectException(F f, std::source_location location = std::source_location::current())
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
	catch(const Exception&)
	{
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

	void logError(std::string_view testName, std::string_view testCaseName, std::string_view message)
	{
			std::cerr << "ERROR: " << testName << "::" << testCaseName << " - " << message << std::endl;
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

	void execute(std::string_view testName, std::string_view testCaseName, std::function<void()> func, ResultLogger& logger)
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
			logger.logError(testName, testCaseName, std::string("Unhandled std::exception: ") + e.what());
		}
		catch(...)
		{
			logger.logError(testName, testCaseName, "Unhandled unknown exception");
		}

		m_results.add(testName, passed);
	}

	auto results() -> const TestResults&{ return m_results; }

private:
	TestResults m_results;
};

class TestSuiteInterface{
public:
	virtual ~TestSuiteInterface() = default;
	virtual void executeAll(TestExecutor& executor, ResultLogger& logger) const = 0;
	virtual void executeTestCase(TestExecutor& executor, std::string_view name, ResultLogger& logger) const = 0;
};
using TestSuitePtr = std::unique_ptr<TestSuiteInterface>;

template<typename... Args>
class TestSuite : public TestSuiteInterface{
public:
	using TestFunc  = std::function<void(Args...)>;
	using TupleType = std::tuple<std::decay_t<Args>...>;

	struct TestCase{
		std::string name;
		TupleType   args;
	};

	TestSuite(std::string testName, TestFunc testFunc)
		: m_testName{std::move(testName)}
		, m_testFunc{std::forward<TestFunc>(testFunc)}
	{
	}

	TestSuite(const TestSuite&) = delete;
	TestSuite& operator=(const TestSuite&) = delete;

	void addTestCase(std::string name, Args&&... args)
	{
		m_testCases.push_back({std::move(name), TupleType(std::forward<Args>(args)...)});
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
			throw std::logic_error("Test suite '" + m_testName + "' does not have any test cases");

		for(const auto& testCase : m_testCases)
		{
			executor.execute(m_testName, testCase.name,
				[this, &testCase]()
				{
					std::apply(m_testFunc, testCase.args);
				},
				logger);
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
			logger);
	}

private:
	std::string           m_testName;
	TestFunc              m_testFunc;
	std::vector<TestCase> m_testCases;
};

class TestApp{
public:
	template<typename F>
	[[nodiscard]] auto& addTest(std::string name, F&& testFunc)
	{
		auto* testSuite = new TestSuite(std::move(name), std::function(std::forward<F>(testFunc)));
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

}
