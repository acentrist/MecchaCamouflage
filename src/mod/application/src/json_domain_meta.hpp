#pragma once

#include <meccha/core/config.hpp>

#include <glaze/glaze.hpp>

template <>
struct glz::meta<meccha::core::FunctionKey>
{
    using enum meccha::core::FunctionKey;
    static constexpr auto value = glz::enumerate(
        "F1", F1,
        "F2", F2,
        "F3", F3,
        "F4", F4,
        "F5", F5,
        "F6", F6,
        "F7", F7,
        "F8", F8,
        "F9", F9,
        "F10", F10,
        "F11", F11,
        "F12", F12,
        "F13", F13,
        "F14", F14,
        "F15", F15,
        "F16", F16,
        "F17", F17,
        "F18", F18,
        "F19", F19,
        "F20", F20,
        "F21", F21,
        "F22", F22,
        "F23", F23,
        "F24", F24);
};

template <>
struct glz::meta<meccha::core::RegionMode>
{
    using enum meccha::core::RegionMode;
    static constexpr auto value =
        glz::enumerate("paint", Paint, "fill", Fill, "skip", Skip);
};

template <>
struct glz::meta<meccha::core::BodyProfile>
{
    using enum meccha::core::BodyProfile;
    static constexpr auto value = glz::enumerate(
        "round", Round,
        "cube", Cube,
        "fukuyoka", Fukuyoka);
};

template <>
struct glz::meta<meccha::core::PlacementMode>
{
    using enum meccha::core::PlacementMode;
    static constexpr auto value =
        glz::enumerate("fit", Fit, "fill", Fill);
};

template <>
struct glz::meta<meccha::core::FaceBaseMode>
{
    using enum meccha::core::FaceBaseMode;
    static constexpr auto value =
        glz::enumerate("fill", Fill, "skip", Skip);
};

template <>
struct glz::meta<meccha::core::AlphaMode>
{
    using enum meccha::core::AlphaMode;
    static constexpr auto value =
        glz::enumerate("skip", Skip, "background", Background);
};

template <>
struct glz::meta<meccha::core::ImageMime>
{
    using enum meccha::core::ImageMime;
    static constexpr auto value = glz::enumerate(
        "image/png", Png,
        "image/jpeg", Jpeg,
        "image/webp", WebP);
};

template <>
struct glz::meta<meccha::core::EspScope>
{
    using enum meccha::core::EspScope;
    static constexpr auto value =
        glz::enumerate("all", All, "hider", Hider, "hunter", Hunter);
};
