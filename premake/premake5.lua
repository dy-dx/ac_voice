
solution "ac_voice"
    basedir ".."
    location (_ACTION)
    targetdir "../bin"
    startproject "ac_voice"
    configurations { "Debug", "Release" }
    platforms "x64"
    flags { "MultiProcessorCompile", "Symbols" }

    defines "_CRT_SECURE_NO_WARNINGS"
    configuration "Debug"
        defines { "DEBUG" }
    configuration "Release"
        defines { "NDEBUG" }
        optimize "Full"
    configuration {}

    project "ac_voice"
        kind "ConsoleApp"
        language "C++"
        configuration "gmake"
            buildoptions { "-std=c++14" }
        configuration {}
        files
        {
            "../ac_voice/**.h",
            "../ac_voice/**.cpp"
        }
        includedirs
        {
            "../ac_voice"
        }
        links
        {
        }
        debugargs
        {
            "voices/soho"
        }
    project "*"
