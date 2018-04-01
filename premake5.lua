workspace "d3d9-wrapper"
   configurations { "Release", "Debug" }
   platforms { "Win32" }
   architecture "x32"
   location "build"
   objdir ("build/obj")
   buildlog ("build/log/%{prj.name}.log")
   
   kind "SharedLib"
   language "C++"
   targetdir "data"
   targetname "d3d9"
   targetextension ".dll"
   characterset ("MBCS")
   flags { "StaticRuntime" }
   
   defines { "rsc_CompanyName=\"ThirteenAG\"" }
   defines { "rsc_LegalCopyright=\"MIT License\""} 
   defines { "rsc_FileVersion=\"1.0.0.0\"", "rsc_ProductVersion=\"1.0.0.0\"" }
   defines { "rsc_InternalName=\"%{prj.name}\"", "rsc_ProductName=\"%{prj.name}\"", "rsc_OriginalFilename=\"d3d9.dll\"" }
   defines { "rsc_FileDescription=\"https://thirteenag.github.io/wfp\"" }
   defines { "rsc_UpdateUrl=\"https://github.com/ThirteenAG/%{prj.name}\"" }
   
   files { "source/*.h", "source/*.cpp" }
   files { "source/*.def" }
   files { "source/*.rc" }
      
   filter "configurations:Debug"
      defines "DEBUG"
      symbols "On"

   filter "configurations:Release"
      defines "NDEBUG"
      optimize "On"

project "d3d9-wrapper"