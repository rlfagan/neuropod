//
// Uber, Inc. (c) 2019
//

// Don't run infer on this file
// NEUROPOD_CI_SKIP_INFER

#include "benchmark/benchmark.h"
#include "neuropod/neuropod.hh"

namespace
{

struct load_in_process
{
    std::unique_ptr<neuropod::Neuropod> operator()(const std::string &path)
    {
        return neuropod::stdx::make_unique<neuropod::Neuropod>(path);
    }
};

struct load_out_of_process
{
    std::unique_ptr<neuropod::Neuropod> operator()(const std::string &path)
    {
        neuropod::RuntimeOptions opts;
        opts.use_ope = true;
        return neuropod::stdx::make_unique<neuropod::Neuropod>(path, opts);
    }
};

} // namespace

template <typename Loader>
void benchmark_object_detection(benchmark::State &state)
{
    const uint8_t some_image_data[1200 * 1920 * 3] = {0};

    auto neuropod = Loader()("neuropod/tests/test_data/dummy_object_detection/");

    for (auto _ : state)
    {
        neuropod::NeuropodValueMap input_data;

        // Add an input "image"
        auto image_tensor = neuropod->template allocate_tensor<uint8_t>({1200, 1920, 3});
        image_tensor->copy_from(some_image_data, 1200 * 1920 * 3);
        input_data["image"] = image_tensor;

        // Run inference
        const auto output_data = neuropod->infer(input_data);

        // Make sure we don't optimize it out
        benchmark::DoNotOptimize(output_data);
    }
}

BENCHMARK_TEMPLATE(benchmark_object_detection, load_in_process);
BENCHMARK_TEMPLATE(benchmark_object_detection, load_out_of_process);

template <typename Loader>
void benchmark_small_inputs(benchmark::State &state)
{
    const float some_data[10 * 5] = {0};

    auto neuropod = Loader()("neuropod/tests/test_data/dummy_small_input_model/");

    for (auto _ : state)
    {
        neuropod::NeuropodValueMap input_data;

        for (int i = 0; i < 100; i++)
        {
            // Add all the inputs
            auto tensor = neuropod->template allocate_tensor<float>({10, 5});
            tensor->copy_from(some_data, 10 * 5);
            input_data["small_input" + std::to_string(i)] = tensor;
        }

        // Run inference
        const auto output_data = neuropod->infer(input_data);

        // Make sure we don't optimize it out
        benchmark::DoNotOptimize(output_data);
    }
}

BENCHMARK_TEMPLATE(benchmark_small_inputs, load_in_process);
BENCHMARK_TEMPLATE(benchmark_small_inputs, load_out_of_process);

void benchmark_ope_scaling_round_robin(benchmark::State &state)
{
    const uint8_t some_image_data[1200 * 1920 * 3] = {0};

    // Create models
    std::vector<neuropod::Neuropod> models;
    for (int i = 0; i < state.range(0); i++)
    {
        neuropod::RuntimeOptions opts;
        opts.use_ope = true;
        models.emplace_back("neuropod/tests/test_data/dummy_object_detection/", opts);
    }

    size_t counter    = 0;
    size_t num_models = models.size();
    for (auto _ : state)
    {
        // Simple round robin
        auto &neuropod = models.at(counter);
        counter        = (counter + 1) % num_models;

        neuropod::NeuropodValueMap input_data;

        // Add an input "image"
        auto image_tensor = neuropod.allocate_tensor<uint8_t>({1200, 1920, 3});
        image_tensor->copy_from(some_image_data, 1200 * 1920 * 3);
        input_data["image"] = image_tensor;

        // Run inference
        const auto output_data = neuropod.infer(input_data);

        // Make sure we don't optimize it out
        benchmark::DoNotOptimize(output_data);
    }
}

BENCHMARK(benchmark_ope_scaling_round_robin)->DenseRange(1, 8);
