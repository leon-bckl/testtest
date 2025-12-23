#pragma once

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <functional>
#include <iostream>
#include <memory>
#include <source_location>
#include <stdexcept>
#include <string>
#include <tuple>
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

template<typename A, typename B>
void compare(A&& actual, B&& expected, std::source_location location = std::source_location::current())
{
	check(actual == expected, "Comparison failed", location);
}

template<typename Exception, typename F>
void expectException(F f)
{
	try
	{
		f();
		fail("Expected exception but none was thrown");
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
		fail("Expected a different exception type");
	}
}

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

private:
	std::string m_currentTestName;
};

class TestExecutor{
public:
	TestExecutor(std::unique_ptr<ResultLogger> logger)
		: m_resultLogger{std::move(logger)}
	{
	}

	void execute(std::string_view testName, std::string_view testCaseName, std::function<void()> func)
	{
		m_resultLogger->logRunningTest(testName, testCaseName);

		try
		{
			func();
			++m_numSucceeded;
		}
		catch(const TestFailure& e)
		{
			++m_numFailed;
			m_resultLogger->logFailure(testName, testCaseName, e);
		}
		catch(const std::exception& e)
		{
			++m_numFailed;
			m_resultLogger->logError(testName, testCaseName, std::string("Unhandled std::exception: ") + e.what());
		}
		catch(...)
		{
			++m_numFailed;
			m_resultLogger->logError(testName, testCaseName, "Unhandled unknown exception");
		}
	}

	int failedTests() const{ return m_numFailed; }
	int successfulTests() const{ return m_numSucceeded; }
	int totalTests() const{ return failedTests() + successfulTests(); }

private:
	std::unique_ptr<ResultLogger> m_resultLogger;
	int                           m_numFailed    = 0;
	int                           m_numSucceeded = 0;
};

class TestSuiteInterface{
public:
	virtual ~TestSuiteInterface() = default;
	virtual void executeAll(TestExecutor& executor) const = 0;
	virtual void executeTestCase(TestExecutor& executor, std::string_view name) const = 0;
};
using TestSuitePtr = std::unique_ptr<TestSuiteInterface>;

template<typename... Args>
class TestSuite : public TestSuiteInterface{
public:
	using TestFunc = std::function<void(Args...)>;

	struct TestCase{
		std::string         name;
		std::tuple<Args...> args;
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
		m_testCases.push_back({std::move(name), std::make_tuple(std::forward<Args>(args)...)});
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

	void executeAll(TestExecutor& executor) const override
	{
		if(m_testCases.empty())
			throw std::logic_error("Test suite '" + m_testName + "' does not have any test cases");

		for(const auto& testCase : m_testCases)
		{
			executor.execute(m_testName, testCase.name,
				[this, &testCase]()
				{
					std::apply(m_testFunc, testCase.args);
				});
		}
	}

	void executeTestCase(TestExecutor& executor, std::string_view name) const override
	{
		const auto it = std::ranges::find_if(m_testCases, [name=name](const TestCase& t){ return t.name == name; });

		if(it == m_testCases.end())
			throw std::logic_error("Test case '" + std::string(name) + "' does not exist in test suite '" + m_testName + "'");

		executor.execute(m_testName, it->name,
			[this, &testCase=*it]()
			{
				std::apply(m_testFunc, testCase.args);
			});
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

			auto executor = TestExecutor(std::make_unique<ResultLogger>());

			for(const auto& test : m_tests)
				test->executeAll(executor);
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
