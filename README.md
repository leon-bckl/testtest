# Usage

```cpp
#include <ctime>
#include <test/test.h>

int main(int argc, char** argv)
{
  auto app = test::TestApp();

  // Test function without data. Executed once.
  app.addTest("TestFunc", [](){
    test::compare(std::to_string(42), "42");
    test::check(!false, "!false");

    test::expectException<std::runtime_error>([](){
      throw std::runtime_error("expected message");
    }, "expected message");

    const auto time = std::time(NULL);

    if(std::localtime(&time)->tm_wday != 0)
      test::fail("This test only succeeds on Sundays");
  });

  // Test function with parameters. Executed once per data entry.
  app.addTest("TestFuncWithParams", [](int i, const std::string& s){
    test::check(i < 42);
    test::compare(s.size(), 3);
  })({
    // Data name, followed by tuple matching test function parameters.
    {"First",  {10, "foo"}},
    {"Second", {25, "bar"}},
    {"Third",  {38, "abc"}}
  });

  app.main(argc, argv);
}
```
