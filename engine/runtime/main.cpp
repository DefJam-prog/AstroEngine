#include <exception>
#include <iostream>
#include <string_view>

static_assert(__cplusplus >= 202002L, "AstroEngine requires C++20");

int main(int argc, char* argv[])
{
    try
    {
        if (argc == 2 && std::string_view(argv[1]) == "--help")
        {
            std::cout << "Usage: astro_headless [--help]\n" << std::flush;
        }
        else if (argc != 1)
        {
            std::cerr << "[error] Invalid arguments. Usage: astro_headless [--help]\n";
            return 2;
        }
        else
        {
            std::cout << "[info] AstroEngine\n"
                         "[info] Mode: headless\n"
                         "[info] Language: C++20\n"
                         "[info] Startup complete\n"
                      << std::flush;
        }
        if (!std::cout)
        {
            std::cerr << "[error] Failed to write startup output\n";
            return 1;
        }
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "[error] " << error.what() << '\n';
    }
    catch (...)
    {
        std::cerr << "[error] Unknown startup failure\n";
    }
    return 1;
}
