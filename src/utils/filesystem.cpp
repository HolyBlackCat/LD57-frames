#include "filesystem.h"

#include <fmt/format.h>
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_iostream.h>

#ifdef _WIN32
#define MOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace em::Filesystem
{
    zstring_view GetResourceDir()
    {
        // Not sure if we need to copy this or not. Copying just in case.
        static const std::string dir = SDL_GetBasePath();
        return dir;
    }

    #ifdef _WIN32
    std::wstring WindowsUtf8ToWide(zstring_view input)
    {
        // Using WinAPI for now. Could make something manually...

        // This size includes the null terminator if we included it in the input length.
        const int out_size = MultiByteToWideChar(
            /*source encoding:*/CP_UTF8,
            /*flags:*/MB_ERR_INVALID_CHARS, // Fail on invalid input.
            /*input:*/input.c_str(),
            /*input size:*/int(input.size() + 1), // Must include the null terminator here if we want it in the output.
            /*output:*/nullptr,
            /*output size:*/0
        );
        if (!out_size)
            throw std::runtime_error("Unable to convert UTF-8 string to UTF-16, maybe invalid encoding in input?");

        std::wstring ret;
        ret.resize(std::size_t(out_size) - 1); // The null-terminator is added automatically.

        const int result = MultiByteToWideChar(
            /*source encoding:*/CP_UTF8,
            /*flags:*/MB_ERR_INVALID_CHARS, // Fail on invalid input.
            /*input:*/input.c_str(),
            /*input size:*/int(input.size() + 1), // Must include the null terminator here if we want it in the output.
            /*output:*/ret.data(),
            /*output size:*/int(ret.size() + 1) // The string additionally has space for the null-terminator.
        );
        if (result != out_size)
            throw std::runtime_error("Unable to convert UTF-8 string to UTF-16, maybe invalid encoding in input? The initial size calculation passed but the conversion failed.");
        return ret;
    }
    #endif

    FILE *Raw::fopen(zstring_view name, zstring_view mode)
    {
        #ifdef _WIN32
        return _wfopen(WindowsUtf8ToWide(name).c_str(), WindowsUtf8ToWide(mode).c_str());
        #else
        // Need this to support `ftello64()`, which only makes a difference on 32 bits.
        return fopen64(name.c_str(), mode.c_str());
        #endif
    }

    offset_t Raw::ftell(FILE *file)
    {
        #ifdef _WIN32
        return _ftelli64(file);
        #else
        return ftello64(file);
        #endif
    }

    int Raw::fseek(FILE *file, offset_t offset, int origin)
    {
        #ifdef _WIN32
        return _fseeki64(file, offset, origin);
        #else
        return fseeko64(file, offset, origin);
        #endif
    }

    File::File(zstring_view name, zstring_view mode, offset_t *size)
        : File() // Ensure cleanup on throw.
    {
        handle = Raw::fopen(name, mode);
        if (!handle)
            throw std::runtime_error(fmt::format("Unable to open file `{}` with mode `{}`.", name, mode));

        if (size)
        {
            SetCurrentPos(0, SeekMode::end);
            *size = CurrentPos();
            SetCurrentPos(0, SeekMode::absolute);
        }
    }

    File::~File()
    {
        if (handle)
            std::fclose(handle); // No special closing function.
    }

    offset_t File::CurrentPos() const
    {
        offset_t pos = Raw::ftell(handle);
        if (pos < 0) // Returns `-1` on failure.
            throw std::runtime_error("Unable to get the current file position.");
        return pos;
    }

    void File::SetCurrentPos(offset_t pos, SeekMode mode)
    {
        int result = Raw::fseek(handle, pos, int(mode));
        if (result)
            throw std::runtime_error("Unable to set the file position.");
    }

    #if 0 // A manual implementation of loading a file to string, in case we need it later.
    std::string ReadFile(zstring_view name, zstring_view mode)
    {
        offset_t size = 0;
        File file(name, mode, &size);

        std::string ret;
        bool ok = false;
        ret.resize_and_overwrite(std::size_t(size), [&](char *p, std::size_t)
        {
            // We shouldn't throw here, cppreference says it's UB.
            // Since we set count to 1, this will return 1 on success.
            ok = std::fread(p, std::size_t(size), 1, file.Handle()) == 1;
            return ok ? size : 0;
        });
        if (!ok)
            throw std::runtime_error(fmt::format("Failed to read the contents of file `{}` using mode `{}`.", name, mode));

        std::getc(file.Handle());
        if (!std::feof(file.Handle()))
            throw std::runtime_error(fmt::format("Failed to read the contents of file `{}` using mode `{}`. It has more contents than its reported size.", name, mode));

        return ret;
    }
    #endif

    LoadedFile::LoadedFile(zstring_view file_path)
    {
        auto new_ptr = reinterpret_cast<const unsigned char *>(SDL_LoadFile(file_path.c_str(), &data_size.value));
        if (!new_ptr)
            throw std::runtime_error(fmt::format("Unable to load file contents: `{}`.", file_path));
        ptr = std::shared_ptr<const unsigned char[]>(new_ptr, [](const unsigned char *data){SDL_free(const_cast<unsigned char *>(data));});

        name = file_path;
    }
}
