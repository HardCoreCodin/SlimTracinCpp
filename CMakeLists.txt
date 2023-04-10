cmake_minimum_required(VERSION 3.11)

project(01_Lights)
add_executable(01_Lights WIN32 src/examples/01_Lights.cpp)

project(02_Geometry)
add_executable(02_Geometry WIN32 src/examples/02_Geometry.cpp)

project(03_BlinnPhong)
add_executable(03_BlinnPhong WIN32 src/examples/03_BlinnPhong.cpp)

project(04_GlassMirror)
add_executable(04_GlassMirror WIN32 src/examples/04_GlassMirror.cpp)

project(05_PBR)
add_executable(05_PBR WIN32 src/examples/05_PBR.cpp)

project(06_AreaLights)
add_executable(06_AreaLights WIN32 src/examples/06_AreaLights.cpp)

project(07_Meshes)
add_executable(07_Meshes WIN32 src/examples/07_Meshes.cpp)

project(obj2mesh)
add_executable(obj2mesh src/obj2mesh.cpp)

project(bmp2texture)
add_executable(bmp2texture src/bmp2texture.cpp)

include(CheckLanguage)
check_language(CUDA)
if(DEFINED CMAKE_CUDA_COMPILER)
    enable_language(CUDA)
    if(NOT DEFINED CMAKE_CUDA_STANDARD)
        set(CMAKE_CUDA_STANDARD 11)
        set(CMAKE_CUDA_STANDARD_REQUIRED ON)
    endif()
    if(NOT DEFINED CMAKE_CUDA_ARCHITECTURES)
        set(CMAKE_CUDA_ARCHITECTURES 52)
    endif()
    set(CUDA_VERBOSE_BUILD ON)

    project(01_Lights_XPU CUDA)
    add_executable(01_Lights_XPU WIN32 src/examples/01_Lights.cu)
    set_target_properties(01_Lights_XPU PROPERTIES CUDA_SEPARABLE_COMPILATION ON)

    project(02_Geometry_XPU CUDA)
    add_executable(02_Geometry_XPU WIN32 src/examples/02_Geometry.cu)
    set_target_properties(02_Geometry_XPU PROPERTIES CUDA_SEPARABLE_COMPILATION ON)

    project(03_BlinnPhong_XPU CUDA)
    add_executable(03_BlinnPhong_XPU WIN32 src/examples/03_BlinnPhong.cu)
    set_target_properties(03_BlinnPhong_XPU PROPERTIES CUDA_SEPARABLE_COMPILATION ON)

    project(04_GlassMirror_XPU CUDA)
    add_executable(04_GlassMirror_XPU WIN32 src/examples/04_GlassMirror.cu)
    set_target_properties(04_GlassMirror_XPU PROPERTIES CUDA_SEPARABLE_COMPILATION ON)

    project(05_PBR_XPU CUDA)
    add_executable(05_PBR_XPU WIN32 src/examples/05_PBR.cu)
    set_target_properties(05_PBR_XPU PROPERTIES CUDA_SEPARABLE_COMPILATION ON)

    project(06_AreaLights_XPU CUDA)
    add_executable(06_AreaLights_XPU WIN32 src/examples/06_AreaLights.cu)
    set_target_properties(06_AreaLights_XPU PROPERTIES CUDA_SEPARABLE_COMPILATION ON)

    project(07_Meshes_XPU CUDA)
    add_executable(07_Meshes_XPU WIN32 src/examples/07_Meshes.cu)
    set_target_properties(07_Meshes_XPU PROPERTIES CUDA_SEPARABLE_COMPILATION ON)
endif()


#add_compile_options(
#        $<$<CONFIG:RELEASE>:-O3>
#        $<$<CONFIG:RELEASE>:-Oi>
#        $<$<CONFIG:RELEASE>:-Mt>
#        $<$<CONFIG:DEBUG>:-JMC>
#        $<$<CONFIG:DEBUG>:-MTd>
#        $<$<CONFIG:DEBUG>:-Z7>
#        -WX-
#        -W4
#        -wd4100
#        -GR-
#        -EHa-
#        -GL
#        -GS
#        -FC
#        -permissive-
#        -sdl
#        -nologo
#        -analyze-
#        -fp:fast
#        -Zc:inline
#        -diagnostics:caret
#        -errorReport:none
#)


#target_compile_options(07_Meshes PUBLIC
#        $<$<CONFIG:RELEASE>:-O3>
#        $<$<CONFIG:RELEASE>:-Oi>
#        $<$<CONFIG:RELEASE>:-Mt>
#        $<$<CONFIG:DEBUG>:-JMC>
#        $<$<CONFIG:DEBUG>:-MTd>
#        $<$<CONFIG:DEBUG>:-Z7>
#        -WX-
#        -W4
#        -wd4100
#        -GR-
#        -EHa-
#        -GL
#        -GS
#        -FC
#        -permissive-
#        -sdl
#        -nologo
#        -analyze-
#        -fp:fast
#        -Zc:inline
#        -diagnostics:caret
#        -errorReport:none
#        )