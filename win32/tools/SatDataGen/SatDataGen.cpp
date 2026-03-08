#include <iostream>
#include <string>
#include <filesystem>

namespace fs = std::filesystem;

int main(int argc, char* argv[])
{
    // Default output dir: SatData inside win32/
    std::string outDir = "SatData";

    // Optional: allow --out <dir>
    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "--out" && i + 1 < argc)
        {
            outDir = argv[++i];
        }
    }

    fs::path exe = fs::absolute(argv[0]);
    fs::path root = exe.parent_path(); // Release Unicode
    root = root.parent_path();         // x64
    root = root.parent_path();         // SatDataGen
    root = root.parent_path();         // tools
    root = root.parent_path();         // win32

    fs::path templateDir = root / "tools" / "SatDataTemplate";
    fs::path targetDir = root / outDir;

    try
    {
        if (!fs::exists(templateDir))
        {
            std::cerr << "SatData template not found at: " << templateDir << "\n";
            return 1;
        }

        fs::create_directories(targetDir);

        for (auto& entry : fs::directory_iterator(templateDir))
        {
            if (!entry.is_regular_file())
                continue;

            fs::path src = entry.path();
            fs::path dst = targetDir / src.filename();

            fs::copy_file(src, dst, fs::copy_options::overwrite_existing);
        }

        std::cout << "SatData generated at: " << targetDir << "\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error generating SatData: " << e.what() << "\n";
        return 1;
    }
}

