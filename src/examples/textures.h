#pragma once

#include "../slim/core/texture.h"
#include "../slim/core/string.h"

enum TextureID {
    Floor_Albedo,
    Floor_Normal,

    Dog_Albedo,
    Dog_Normal,

    Cathedral_SkyboxColor,
    Cathedral_SkyboxRadiance,
    Cathedral_SkyboxIrradiance,

    Bolonga_SkyboxColor,
    Bolonga_SkyboxRadiance,
    Bolonga_SkyboxIrradiance,

    TextureCount
};

Texture textures[TextureCount];
char string_buffers[TextureCount][200]{};
String texture_files[TextureCount]{
    String::getFilePath("floor_albedo.texture",string_buffers[Floor_Albedo],__FILE__),
    String::getFilePath("floor_normal.texture",string_buffers[Floor_Normal],__FILE__),

    String::getFilePath("dog_albedo.texture",string_buffers[Dog_Albedo],__FILE__),
    String::getFilePath("dog_normal.texture",string_buffers[Dog_Normal],__FILE__),

    String::getFilePath("cathedral_color.texture",string_buffers[Cathedral_SkyboxColor],__FILE__),
    String::getFilePath("cathedral_radiance.texture",string_buffers[Cathedral_SkyboxRadiance],__FILE__),
    String::getFilePath("cathedral_irradiance.texture",string_buffers[Cathedral_SkyboxIrradiance],__FILE__),

    String::getFilePath("bolonga_color.texture",string_buffers[Bolonga_SkyboxColor],__FILE__),
    String::getFilePath("bolonga_radiance.texture",string_buffers[Bolonga_SkyboxRadiance],__FILE__),
    String::getFilePath("bolonga_irradiance.texture",string_buffers[Bolonga_SkyboxIrradiance],__FILE__)
};