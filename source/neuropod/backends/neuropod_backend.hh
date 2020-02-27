//
// Uber, Inc. (c) 2018
//

#pragma once

#include "neuropod/backends/tensor_allocator.hh"
#include "neuropod/internal/backend_registration.hh"
#include "neuropod/internal/deleter.hh"
#include "neuropod/internal/neuropod_loader.hh"
#include "neuropod/internal/neuropod_tensor.hh"
#include "neuropod/internal/tensor_types.hh"

#include <memory>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

namespace neuropod
{

// A map from a tensor name to a pointer to a NeuropodValue
// This is the input and output type of `infer`
using NeuropodValueMap = std::unordered_map<std::string, std::shared_ptr<NeuropodValue>>;

// The interface that every neuropod backend implements
class NeuropodBackend
{
private:
    // Whether or not the underlying model has already been loaded
    bool is_model_loaded_ = false;

public:
    NeuropodBackend();
    NeuropodBackend(const std::string &neuropod_path);
    virtual ~NeuropodBackend();

    // Returns an allocator that can allocate tensors compatible with this backend
    virtual std::shared_ptr<NeuropodTensorAllocator> get_tensor_allocator() = 0;

    // Run inference and get a subset of the outputs
    std::unique_ptr<NeuropodValueMap> infer(const NeuropodValueMap &        inputs,
                                            const std::vector<std::string> &requested_outputs = {});

    // Get the inputs and outputs of this model
    const std::vector<TensorSpec> &get_inputs() const;
    const std::vector<TensorSpec> &get_outputs() const;

    // Get the name of this model.
    const std::string &get_name() const;
    // Get the platform of this model.
    const std::string &get_platform() const;

    // Load the model if it has not already been loaded
    void load_model();

protected:
    // Used to load files in a Neuropod
    std::unique_ptr<NeuropodLoader> loader_;

    // The neuropod model config
    std::unique_ptr<ModelConfig> model_config_;

    // The neuropod path (if one was provided in the constructor)
    std::string neuropod_path_;

    // Run inference and get a subset of the outputs
    // The default implementation runs inference, gets all the outputs, and then filters the outputs
    // Backends can override this to more efficiently generate only the requested outputs
    virtual std::unique_ptr<NeuropodValueMap> infer_internal(const NeuropodValueMap &        inputs,
                                                             const std::vector<std::string> &requested_outputs);

    // Run inference
    // Backends must provide an implementation of infer_internal (either this signature or the one above)
    virtual std::unique_ptr<NeuropodValueMap> infer_internal(const NeuropodValueMap &inputs);

    // A method that loads the underlying model
    virtual void load_model_internal() = 0;
};

template <template <class> class TensorImpl>
class NeuropodBackendWithDefaultAllocator : public NeuropodBackend
{
private:
    std::shared_ptr<NeuropodTensorAllocator> allocator_;

public:
    NeuropodBackendWithDefaultAllocator() : allocator_(std::make_shared<DefaultTensorAllocator<TensorImpl>>()) {}

    NeuropodBackendWithDefaultAllocator(const std::string &neuropod_path)
        : NeuropodBackend(neuropod_path), allocator_(std::make_shared<DefaultTensorAllocator<TensorImpl>>())
    {
    }

    std::shared_ptr<NeuropodTensorAllocator> get_tensor_allocator() { return allocator_; }
};

} // namespace neuropod