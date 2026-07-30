#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.h"

#include "TestHelpers.h"

#include <filesystem>

namespace moe::test
{

const std::string& scratchDirectory()
{
    static const std::string directory = []
    {
        auto path = std::filesystem::temp_directory_path() / "moe-tests";
        std::filesystem::create_directories (path);
        return path.string() + "/";
    }();

    return directory;
}

} // namespace moe::test

int main (int argc, char** argv)
{
    doctest::Context context;
    context.applyCommandLine (argc, argv);

    const int result = context.run();

    // Fixtures are left in place on failure so they can be inspected.
    if (result == 0)
        std::filesystem::remove_all (moe::test::scratchDirectory());

    return result;
}
