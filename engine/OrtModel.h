// Thin wrapper around ONNX Runtime sessions with consistent error reporting.
#pragma once
#include "Protocol.h"
#include <filesystem>
#include <memory>
#include <onnxruntime_cxx_api.h>
#include <string>
#include <vector>

namespace vb
{
struct OrtEnvHolder
{
    static Ort::Env& env()
    {
        static Ort::Env e (ORT_LOGGING_LEVEL_WARNING, "VocalEngine");
        return e;
    }
};

class OrtModel
{
public:
    OrtModel (const std::filesystem::path& file, int threads)
    {
        if (! std::filesystem::exists (file))
            throw EngineError { "MODEL_MISSING", "Model file not found: " + file.filename().string() + ". Reinstall Vocal Builder to restore it." };
        Ort::SessionOptions so;
        so.SetIntraOpNumThreads (threads);
        so.SetInterOpNumThreads (1);
        so.SetGraphOptimizationLevel (GraphOptimizationLevel::ORT_ENABLE_ALL);
        so.DisableCpuMemArena(); // return memory between chunks; keeps peak use lower on long songs
        try
        {
#ifdef _WIN32
            session = std::make_unique<Ort::Session> (OrtEnvHolder::env(), file.wstring().c_str(), so);
#else
            session = std::make_unique<Ort::Session> (OrtEnvHolder::env(), file.string().c_str(), so);
#endif
        }
        catch (const Ort::Exception& e)
        {
            throw EngineError { "MODEL_CORRUPT", "Model file could not be loaded (" + file.filename().string() + "): " + e.what() + ". Reinstall Vocal Builder to restore it." };
        }
        Ort::AllocatorWithDefaultOptions alloc;
        for (size_t i = 0; i < session->GetInputCount(); ++i)
            inputNames.push_back (session->GetInputNameAllocated (i, alloc).get());
        for (size_t i = 0; i < session->GetOutputCount(); ++i)
            outputNames.push_back (session->GetOutputNameAllocated (i, alloc).get());
    }

    std::vector<Ort::Value> run (std::vector<Ort::Value>& inputs)
    {
        std::vector<const char*> in, out;
        for (auto& s : inputNames)
            in.push_back (s.c_str());
        for (auto& s : outputNames)
            out.push_back (s.c_str());
        try
        {
            return session->Run (Ort::RunOptions { nullptr }, in.data(), inputs.data(), inputs.size(), out.data(), out.size());
        }
        catch (const Ort::Exception& e)
        {
            const std::string what = e.what();
            if (what.find ("alloc") != std::string::npos || what.find ("memory") != std::string::npos)
                throw EngineError { "OUT_OF_MEMORY", "Not enough memory to process this audio. Close other programs or process a shorter section." };
            throw EngineError { "INFERENCE_FAILED", "Model inference failed: " + what };
        }
    }

    static Ort::Value tensor (std::vector<float>& data, const std::vector<int64_t>& shape)
    {
        static Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu (OrtArenaAllocator, OrtMemTypeDefault);
        return Ort::Value::CreateTensor<float> (mem, data.data(), data.size(), shape.data(), shape.size());
    }

    static Ort::Value tensorI64 (std::vector<int64_t>& data, const std::vector<int64_t>& shape)
    {
        static Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu (OrtArenaAllocator, OrtMemTypeDefault);
        return Ort::Value::CreateTensor<int64_t> (mem, data.data(), data.size(), shape.data(), shape.size());
    }

    std::vector<std::string> inputNames, outputNames;

private:
    std::unique_ptr<Ort::Session> session;
};
} // namespace vb
