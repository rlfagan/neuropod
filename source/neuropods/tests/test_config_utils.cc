//
// Uber, Inc. (c) 2018
//

#include "gtest/gtest.h"

#include "neuropods/internal/config_utils.hh"

namespace
{

// UATG(clang-format/format) intentionally formatted
const std::string VALID_SPEC = \
"{"
"  \"name\": \"addition_model\","
"  \"platform\": \"tensorflow\","
"  \"input_spec\": [{"
"    \"dtype\": \"float32\","
"    \"shape\": [null, 2, \"some_symbol\"],"
"    \"name\": \"x\""
"  }],"
"  \"output_spec\": [{"
"    \"dtype\": \"float32\","
"    \"shape\": [null, 2, \"some_symbol\"],"
"    \"name\": \"y\""
"  }]"
"}";

std::string replace(const std::string &search, const std::string &replace)
{
    // Replace `search` with `replace` in the config above
    std::string spec = VALID_SPEC;
    return spec.replace(spec.find(search), search.length(), replace);
}

} // namespace


TEST(test_config_utils, valid_config)
{
    // Test that the config is valid
    std::istringstream config(VALID_SPEC);
    neuropods::load_model_config(config);
}


TEST(test_config_utils, invalid_name)
{
    // Name must be a string
    std::istringstream config(replace("\"addition_model\"", "true"));
    EXPECT_THROW(neuropods::load_model_config(config), std::runtime_error);
}

TEST(test_config_utils, invalid_platform)
{
    // Platform must be a string
    std::istringstream config(replace("\"tensorflow\"", "5"));
    EXPECT_THROW(neuropods::load_model_config(config), std::runtime_error);
}


TEST(test_config_utils, invalid_spec_dtype)
{
    // complex128 is not a supported type
    std::istringstream config(replace("float32", "complex128"));
    EXPECT_THROW(neuropods::load_model_config(config), std::runtime_error);
}

TEST(test_config_utils, invalid_spec_name)
{
    // The name of a tensor must be a string
    std::istringstream config(replace("\"x\"", "true"));
    EXPECT_THROW(neuropods::load_model_config(config), std::runtime_error);
}

TEST(test_config_utils, invalid_spec_shape)
{
    // "123" is not a valid shape. Must be an array
    std::istringstream config(replace("[null, 2, \"some_symbol\"]", "\"123\""));
    EXPECT_THROW(neuropods::load_model_config(config), std::runtime_error);
}

TEST(test_config_utils, invalid_spec_shape_element)
{
    // true is not valid in a shape
    std::istringstream config(replace("[null, 2, \"some_symbol\"]", "[null, 2, \"some_symbol\", true]"));
    EXPECT_THROW(neuropods::load_model_config(config), std::runtime_error);
}
