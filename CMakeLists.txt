cmake_minimum_required(VERSION 3.8)

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

project(08_Modes)
add_executable(08_Modes WIN32 src/examples/08_Modes.cpp)

project(09_Textures)
add_executable(09_Textures WIN32 src/examples/09_Textures.cpp)

project(obj2mesh)
add_executable(obj2mesh src/obj2mesh.cpp)

project(bmp2texture)
add_executable(bmp2texture src/bmp2texture.cpp)