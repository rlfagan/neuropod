//
// Uber, Inc. (c) 2019
//

#include "neuropod/multiprocess/multiprocess.hh"

#include "neuropod/backends/neuropod_backend.hh"
#include "neuropod/internal/logging.hh"
#include "neuropod/multiprocess/control_messages.hh"
#include "neuropod/multiprocess/ipc_control_channel.hh"
#include "neuropod/multiprocess/shm_tensor.hh"

#include <boost/date_time/microsec_time_clock.hpp>
#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <sys/wait.h>

#include <vector>

#include <signal.h>
#include <spawn.h>

extern char **environ;

namespace neuropod
{

namespace
{

// A utility to get the environment as a map
std::unordered_map<std::string, std::string> get_env_map()
{
    std::unordered_map<std::string, std::string> env;
    for (char **current = environ; *current; current++)
    {
        std::string item = *current;
        const auto  pos  = item.find("=");
        if (pos == std::string::npos)
        {
            // No `=` found
            continue;
        }

        const auto key = item.substr(0, pos);  // Not including the `=`
        const auto val = item.substr(pos + 1); // Not including the `=`

        env[key] = val;
    }

    return env;
}

// Start a neuropod worker process given a control queue name
pid_t start_worker_process(const std::string &control_queue_name, std::vector<std::string> env)
{
    pid_t child_pid;
    char *argv[] = {
        const_cast<char *>("neuropod_multiprocess_worker"), const_cast<char *>(control_queue_name.c_str()), NULL};

    // Setup the environment

    // Null terminated char * array
    char *env_arr[env.size() + 1];
    env_arr[env.size()] = nullptr;

    // Set the env
    for (int i = 0; i < env.size(); i++)
    {
        env_arr[i] = const_cast<char *>(env[i].c_str());
    }

    // Spawn a process
    const auto status = posix_spawnp(&child_pid, "neuropod_multiprocess_worker", NULL, NULL, argv, env_arr);
    if (status != 0)
    {
        NEUROPOD_ERROR("Failed to start the worker process. Failed with code: {} - {}", status, strerror(status));
    }

    return child_pid;
}

// Note: we don't register this with the library as a backend because it is not
// a backend in the normal sense. It is only used here for out of process
// execution

class MultiprocessNeuropodBackend : public NeuropodBackendWithDefaultAllocator<SHMNeuropodTensor>
{
private:
    pid_t       child_pid_ = -1;
    std::string control_queue_name_;
    bool        free_memory_every_cycle_;

    // Control channel for interacting with the worker
    IPCControlChannel control_channel_;

    void wait_for_load_confirmation(const std::string &neuropod_path)
    {
        // Wait for confirmation that the model was loaded
        while (true)
        {
            // Get a message from the worker
            ControlMessage received;
            bool           successful_read = control_channel_.recv_message(received, MESSAGE_TIMEOUT_MS);
            if (!successful_read)
            {
                // We timed out
                NEUROPOD_ERROR("Timed out waiting for the worker process to load: {}. Didn't receive a message in "
                               "{}ms, but expected a heartbeat every {}ms.",
                               neuropod_path,
                               MESSAGE_TIMEOUT_MS,
                               HEARTBEAT_INTERVAL_MS);
            }

            auto msg_type = received.get_type();

            if (msg_type == LOAD_SUCCESS)
            {
                // The model was successfully loaded
                break;
            }

            if (msg_type == HEARTBEAT)
            {
                // TODO(vip): Also periodically check for a heartbeat
                continue;
            }

            // We got an unexpected message
            NEUROPOD_ERROR("Expected LOAD_SUCCESS, but got unexpected message from the worker process: {}", msg_type);
        }
    }

public:
    MultiprocessNeuropodBackend(const std::string &neuropod_path,
                                const std::string &control_queue_name,
                                bool               free_memory_every_cycle)
        : NeuropodBackendWithDefaultAllocator<SHMNeuropodTensor>(neuropod_path),
          control_queue_name_(control_queue_name),
          free_memory_every_cycle_(free_memory_every_cycle),
          control_channel_(control_queue_name, MAIN_PROCESS)
    {
        load_model();
    }

    // Generate a control queue name and start a worker
    MultiprocessNeuropodBackend(const std::string &   neuropod_path,
                                const RuntimeOptions &options,
                                bool                  free_memory_every_cycle)
        : NeuropodBackendWithDefaultAllocator<SHMNeuropodTensor>(neuropod_path),
          control_queue_name_(boost::uuids::to_string(boost::uuids::random_generator()())),
          free_memory_every_cycle_(free_memory_every_cycle),
          control_channel_(control_queue_name_, MAIN_PROCESS)
    {
        auto env = get_env_map();

        // Set the visible devices correctly when starting the worker process
        if (options.visible_device == Device::CPU)
        {
            env["CUDA_VISIBLE_DEVICES"] = "";
        }
        else
        {
            env["CUDA_VISIBLE_DEVICES"] = std::to_string(options.visible_device);
        }

        // Convert to a vector
        std::vector<std::string> env_vec;
        env_vec.reserve(env.size());
        for (const auto &item : env)
        {
            env_vec.emplace_back(item.first + "=" + item.second);
        }

        // Start the worker process
        child_pid_ = start_worker_process(control_queue_name_, env_vec);

        if (options.load_model_at_construction)
        {
            load_model();
        }
    }

    ~MultiprocessNeuropodBackend()
    {
        // We only need to clean up all of this if we started the worker process
        if (child_pid_ > 0)
        {
            // Ask the child process to shutdown
            control_channel_.send_message(SHUTDOWN);

            // Wait for it and make sure it exited properly
            int status;
            waitpid(child_pid_, &status, 0);
            if (WIFEXITED(status))
            {
                const auto exit_code = WEXITSTATUS(status);
                if (exit_code != 0)
                {
                    // We don't want to throw an error in the destructor so we'll just log for now
                    std::cerr << "Worker process exited abnormally. Exit code: " << exit_code << std::endl;
                }
            }
            else if (WIFSIGNALED(status))
            {
                // We don't want to throw an error in the destructor so we'll just log for now
                std::cerr << "Worker process exited abnormally. Was terminated by signal: " << WTERMSIG(status)
                          << std::endl;
            }
            else
            {
                // We don't want to throw an error in the destructor so we'll just log for now
                std::cerr << "Worker process exited abnormally." << std::endl;
            }

            // Delete the control channels
            control_channel_.cleanup();
        }
    }

protected:
    // Run inference
    std::unique_ptr<NeuropodValueMap> infer_internal(const NeuropodValueMap &        inputs,
                                                     const std::vector<std::string> &requested_outputs)
    {
        // Add inputs
        control_channel_.send_message(ADD_INPUT, inputs);

        // Request outputs
        // TODO(vip): Don't send this message if requested_outputs is empty
        control_channel_.send_message(REQUEST_OUTPUT, requested_outputs);

        // Run inference
        control_channel_.send_message(INFER);

        // Get the outputs from the worker
        auto to_return = stdx::make_unique<NeuropodValueMap>();
        while (true)
        {
            // Get a message from the worker
            SPDLOG_DEBUG("OPE: Waiting for load confirmation from worker...");
            ControlMessage received;
            bool           successful_read = control_channel_.recv_message(received, MESSAGE_TIMEOUT_MS);
            if (!successful_read)
            {
                // We timed out
                NEUROPOD_ERROR("Timed out waiting for a response from worker process. "
                               "Didn't receive a message in {}ms, but expected a heartbeat every {}ms.",
                               MESSAGE_TIMEOUT_MS,
                               HEARTBEAT_INTERVAL_MS);
            }

            auto msg_type = received.get_type();

            if (msg_type == END_OUTPUT)
            {
                // Got all the outputs
                break;
            }

            if (msg_type == HEARTBEAT)
            {
                // TODO(vip): Also periodically check for a heartbeat
                continue;
            }

            if (msg_type != RETURN_OUTPUT)
            {
                NEUROPOD_ERROR("Got unexpected message from the worker process: {}", msg_type);
            }

            // Load the returned tensors
            received.get_valuemap(*to_return);
        }

        // Inference is complete
        // Let the worker know it no longer needs to keep references to the output
        // tensors
        control_channel_.send_message(INFER_COMPLETE);

        if (free_memory_every_cycle_)
        {
            // Clean up any unused shm tensors that haven't been reused
            shm_allocator.free_unused_shm_blocks();
        }

        return to_return;
    }

protected:
    void load_model_internal()
    {
        // Create a message to tell the worker process to load the specified neuropod
        ope_load_config config;
        config.neuropod_path = neuropod_path_;

        // Send the message
        control_channel_.send_message(LOAD_NEUROPOD, config);

        // Wait until the worker process confirms it has loaded the model
        wait_for_load_confirmation(neuropod_path_);
    }
};

} // namespace

std::unique_ptr<Neuropod> load_neuropod_in_new_process(const std::string &   neuropod_path,
                                                       const RuntimeOptions &options,
                                                       bool                  free_memory_every_cycle)
{
    auto backend = std::make_shared<MultiprocessNeuropodBackend>(neuropod_path, options, free_memory_every_cycle);
    return stdx::make_unique<Neuropod>(neuropod_path, backend);
}

std::unique_ptr<Neuropod> load_neuropod_in_worker(const std::string &neuropod_path,
                                                  const std::string &control_queue_name,
                                                  bool               free_memory_every_cycle)
{
    auto backend =
        std::make_shared<MultiprocessNeuropodBackend>(neuropod_path, control_queue_name, free_memory_every_cycle);
    return stdx::make_unique<Neuropod>(neuropod_path, backend);
}

} // namespace neuropod