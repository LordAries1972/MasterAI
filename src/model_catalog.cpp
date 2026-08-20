// MasterAI curated downloadable-model catalog -- generated data.
//
// This file holds the single-source-of-truth list model_catalog.hpp
// declares, ported verbatim (every id/tier/label/category/modelId/
// filename/sourceUrl/revision/sha256/minRam/recRam/sizeBytes/
// displayName/architecture/quantization/licenseSpdx field, byte-for-byte)
// from the JS PRESETS literal src/web_ui.cpp used to hardcode before this
// pass -- see model_catalog.hpp for why this now exists as one C++
// structure instead. Entries stay in their original authored order and
// grouping.
#include "model_catalog.hpp"

namespace masterai {

const std::vector<ModelCatalogEntry>& model_catalog() {
    static const std::vector<ModelCatalogEntry> catalog = {
        {
            "tiny-test", "test", "ggml-org/models tinyllamas stories260K (~1 MB)",
            "general-programming", "stories260k-test", "stories260K.gguf",
            "https://huggingface.co/ggml-org/models/resolve/499bc8821c6b12b4e53c5bffcb21ec206f212d81/tinyllamas/stories260K.gguf",
            "499bc8821c6b12b4e53c5bffcb21ec206f212d81",
            "270cba1bd5109f42d03350f60406024560464db173c0e387d91f0426d3bd256d",
            64ULL, 128ULL, 1185376ULL,
            "TinyStories 260K (test fixture)", "llama",
            "F32", "MIT"
        },
        {
            "qwen25-1.5b-q4km", "2", "Qwen2.5-Coder-1.5B-Instruct Q4_K_M (~1.1 GiB)",
            "general-programming", "qwen25-coder-1.5b-q4km", "qwen2.5-coder-1.5b-instruct-q4_k_m.gguf",
            "https://huggingface.co/Qwen/Qwen2.5-Coder-1.5B-Instruct-GGUF/resolve/f86cb2c1fa58255f8052cc32aeede1b7482d4361/qwen2.5-coder-1.5b-instruct-q4_k_m.gguf",
            "f86cb2c1fa58255f8052cc32aeede1b7482d4361",
            "cc324af070c2ecbfd324a30884d2f951a7ff756aba85cb811a6ec436933bb046",
            1536ULL, 2048ULL, 1117320768ULL,
            "Qwen2.5-Coder-1.5B-Instruct", "qwen2",
            "Q4_K_M", "Apache-2.0"
        },
        {
            "qwen25-3b-q4km", "4", "Qwen2.5-Coder-3B-Instruct Q4_K_M (~2.0 GiB)",
            "general-programming", "qwen25-coder-3b-q4km", "qwen2.5-coder-3b-instruct-q4_k_m.gguf",
            "https://huggingface.co/Qwen/Qwen2.5-Coder-3B-Instruct-GGUF/resolve/f74adce6aa16316c625447af059dbebe4983757c/qwen2.5-coder-3b-instruct-q4_k_m.gguf",
            "f74adce6aa16316c625447af059dbebe4983757c",
            "724fb256bec1ff062b2f65e4569e871ad2e95ab2a3989723d1769c54294730b7",
            3072ULL, 4096ULL, 2104932800ULL,
            "Qwen2.5-Coder-3B-Instruct", "qwen2",
            "Q4_K_M", "Apache-2.0"
        },
        {
            "qwen25-7b-q4km", "8", "Qwen2.5-Coder-7B-Instruct Q4_K_M (~4.4 GiB)",
            "general-programming", "qwen25-coder-7b-q4km", "qwen2.5-coder-7b-instruct-q4_k_m.gguf",
            "https://huggingface.co/Qwen/Qwen2.5-Coder-7B-Instruct-GGUF/resolve/13fb94bfda8c8cf22497dc57b78f391a9acb426a/qwen2.5-coder-7b-instruct-q4_k_m.gguf",
            "13fb94bfda8c8cf22497dc57b78f391a9acb426a",
            "509287f78cb4d4cf6b3843734733b914b2c158e43e22a7f4bf5e963800894d3c",
            6144ULL, 8192ULL, 4683073536ULL,
            "Qwen2.5-Coder-7B-Instruct", "qwen2",
            "Q4_K_M", "Apache-2.0"
        },
        {
            "qwen25-14b-q4km", "16", "Qwen2.5-Coder-14B-Instruct Q4_K_M (~8.4 GiB)",
            "general-programming", "qwen25-coder-14b-q4km", "qwen2.5-coder-14b-instruct-q4_k_m.gguf",
            "https://huggingface.co/Qwen/Qwen2.5-Coder-14B-Instruct-GGUF/resolve/d0a692ef765eefbf2fabb130b3cb2e8917e3d225/qwen2.5-coder-14b-instruct-q4_k_m.gguf",
            "d0a692ef765eefbf2fabb130b3cb2e8917e3d225",
            "c1e659736d89ac1065fb495330fb824d94001974a4bfa78e7270e43476a8d940",
            11264ULL, 16384ULL, 8988110272ULL,
            "Qwen2.5-Coder-14B-Instruct", "qwen2",
            "Q4_K_M", "Apache-2.0"
        },
        {
            "qwen25-32b-q4km", "24", "Qwen2.5-Coder-32B-Instruct Q4_K_M (~18.5 GiB)",
            "general-programming", "qwen25-coder-32b-q4km", "qwen2.5-coder-32b-instruct-q4_k_m.gguf",
            "https://huggingface.co/Qwen/Qwen2.5-Coder-32B-Instruct-GGUF/resolve/9d3053fce650fe1cdbdb75998c2a87add9d178ef/qwen2.5-coder-32b-instruct-q4_k_m.gguf",
            "9d3053fce650fe1cdbdb75998c2a87add9d178ef",
            "4d64b316b5e6319d9613e0d97935d9ebd631fc7e334da400d00085eca749d085",
            20480ULL, 24576ULL, 19851335872ULL,
            "Qwen2.5-Coder-32B-Instruct", "qwen2",
            "Q4_K_M", "Apache-2.0"
        },
        {
            "qwen25-32b-q6k", "32", "Qwen2.5-Coder-32B-Instruct Q6_K (~25 GiB)",
            "general-programming", "qwen25-coder-32b-q6k", "qwen2.5-coder-32b-instruct-q6_k.gguf",
            "https://huggingface.co/Qwen/Qwen2.5-Coder-32B-Instruct-GGUF/resolve/9d3053fce650fe1cdbdb75998c2a87add9d178ef/qwen2.5-coder-32b-instruct-q6_k.gguf",
            "9d3053fce650fe1cdbdb75998c2a87add9d178ef",
            "38c1555adabcc7e9dfdae217cfbfdea53c97996a1ca17bd00125cf32bbdc63c2",
            27648ULL, 32768ULL, 26886154432ULL,
            "Qwen2.5-Coder-32B-Instruct", "qwen2",
            "Q6_K", "Apache-2.0"
        },
        {
            "qwen25-32b-q8", "64", "Qwen2.5-Coder-32B-Instruct Q8_0 (~32.4 GiB)",
            "general-programming", "qwen25-coder-32b-q8", "qwen2.5-coder-32b-instruct-q8_0.gguf",
            "https://huggingface.co/Qwen/Qwen2.5-Coder-32B-Instruct-GGUF/resolve/9d3053fce650fe1cdbdb75998c2a87add9d178ef/qwen2.5-coder-32b-instruct-q8_0.gguf",
            "9d3053fce650fe1cdbdb75998c2a87add9d178ef",
            "ae6e5cee79233499b41502ea7270e665dc402b2c35c48d43ef2d5a0a10842725",
            36864ULL, 49152ULL, 34820884672ULL,
            "Qwen2.5-Coder-32B-Instruct", "qwen2",
            "Q8_0", "Apache-2.0"
        },
        {
            "deepseek-coder-1.3b-q4km", "1", "DeepSeek-Coder-1.3B-Instruct Q4_K_M (~0.8 GiB)",
            "general-programming", "deepseek-coder-1.3b-q4km", "deepseek-coder-1.3b-instruct.Q4_K_M.gguf",
            "https://huggingface.co/TheBloke/deepseek-coder-1.3b-instruct-GGUF/resolve/4595af8c3dff738094bd6c86054dfb5a90d5c41e/deepseek-coder-1.3b-instruct.Q4_K_M.gguf",
            "4595af8c3dff738094bd6c86054dfb5a90d5c41e",
            "04cebb6fafa40ae628cf6bfeb76032ec792852f54020c559ad0a56b9f2839118",
            768ULL, 1024ULL, 873582624ULL,
            "", "",
            "", ""
        },
        {
            "codegemma-2b-q4km", "3", "CodeGemma-2B Q4_K_M (~1.5 GiB)",
            "general-programming", "codegemma-2b-q4km", "codegemma-2b-Q4_K_M.gguf",
            "https://huggingface.co/bartowski/codegemma-2b-GGUF/resolve/4c371c2e831eebf3de95e32c841c8b38d4d9502e/codegemma-2b-Q4_K_M.gguf",
            "4c371c2e831eebf3de95e32c841c8b38d4d9502e",
            "4747caaf4dc51a6e7ad6df5fe26f4c485d847998bd58933eaf011f76e5273c14",
            1536ULL, 3072ULL, 1630262400ULL,
            "CodeGemma-2B", "gemma",
            "Q4_K_M", "Gemma"
        },
        {
            "starcoder2-3b-q4km", "3", "StarCoder2-3B Q4_K_M (~1.7 GiB)",
            "general-programming", "starcoder2-3b-q4km", "starcoder2-3b-Q4_K_M.gguf",
            "https://huggingface.co/second-state/StarCoder2-3B-GGUF/resolve/7fca3e2da2ce31df411461e2cb9cae2d2b492f35/starcoder2-3b-Q4_K_M.gguf",
            "7fca3e2da2ce31df411461e2cb9cae2d2b492f35",
            "d8fb39287a463549b80d97473b0a7595c3a5a6da3ae2604ca33906a1a43f7175",
            2048ULL, 3072ULL, 1848976448ULL,
            "", "",
            "", ""
        },
        {
            "phi35-mini-q4km", "3", "Phi-3.5-mini-Instruct Q4_K_M (~2.2 GiB)",
            "general-programming", "phi35-mini-q4km", "Phi-3.5-mini-instruct-Q4_K_M.gguf",
            "https://huggingface.co/bartowski/Phi-3.5-mini-instruct-GGUF/resolve/6d70da17e749a471ccb62ade694486011a75cda3/Phi-3.5-mini-instruct-Q4_K_M.gguf",
            "6d70da17e749a471ccb62ade694486011a75cda3",
            "e4165e3a71af97f1b4820da61079826d8752a2088e313af0c7d346796c38eff5",
            2048ULL, 3072ULL, 2393232672ULL,
            "Phi-3.5-mini-Instruct", "phi3",
            "Q4_K_M", "MIT"
        },
        {
            "deepseek-coder-6.7b-q4km", "5", "DeepSeek-Coder-6.7B-Instruct Q4_K_M (~3.8 GiB)",
            "general-programming", "deepseek-coder-6.7b-q4km", "deepseek-coder-6.7b-instruct.Q4_K_M.gguf",
            "https://huggingface.co/TheBloke/deepseek-coder-6.7B-instruct-GGUF/resolve/9e221e6b41cb1bf1c5d8f9718e81e3dc781f7557/deepseek-coder-6.7b-instruct.Q4_K_M.gguf",
            "9e221e6b41cb1bf1c5d8f9718e81e3dc781f7557",
            "92da6238854f2fa902d8b2ad79d548536af1d3ab06821f323bd5bbcea2013276",
            3840ULL, 5120ULL, 4083015904ULL,
            "", "",
            "", ""
        },
        {
            "codellama-7b-q4km", "5", "CodeLlama-7B-Instruct Q4_K_M (~3.8 GiB)",
            "general-programming", "codellama-7b-q4km", "codellama-7b-instruct.Q4_K_M.gguf",
            "https://huggingface.co/TheBloke/CodeLlama-7B-Instruct-GGUF/resolve/2f064ee0c6ae3f025ec4e392c6ba5dd049c77969/codellama-7b-instruct.Q4_K_M.gguf",
            "2f064ee0c6ae3f025ec4e392c6ba5dd049c77969",
            "0701500c591c2c1b910516658e58044cdfa07b2e8b5a2e3b6808d983441daf1a",
            3840ULL, 5120ULL, 4081095360ULL,
            "", "",
            "", ""
        },
        {
            "starcoder2-7b-q4km", "5", "StarCoder2-7B Q4_K_M (~4.2 GiB)",
            "general-programming", "starcoder2-7b-q4km", "starcoder2-7b.Q4_K_M.gguf",
            "https://huggingface.co/QuantFactory/starcoder2-7b-GGUF/resolve/673788033750e9a7a677072a5184e3c923c19355/starcoder2-7b.Q4_K_M.gguf",
            "673788033750e9a7a677072a5184e3c923c19355",
            "c6f8a3f618bfc1c2cf2172a6d28d43d1e3c3b0489544fd13ef3a73939728d943",
            4096ULL, 5120ULL, 4461280384ULL,
            "", "",
            "", ""
        },
        {
            "yi-coder-9b-q4km", "6", "Yi-Coder-9B-Chat Q4_K_M (~5 GiB)",
            "general-programming", "yi-coder-9b-q4km", "Yi-Coder-9B-Chat-Q4_K_M.gguf",
            "https://huggingface.co/bartowski/Yi-Coder-9B-Chat-GGUF/resolve/2f28bfc370a5457310f3880202b2ed577e2bcbd8/Yi-Coder-9B-Chat-Q4_K_M.gguf",
            "2f28bfc370a5457310f3880202b2ed577e2bcbd8",
            "251cc196e3813d149694f362bb0f8f154f3320abe44724eebe58c23dc54f201d",
            5120ULL, 6144ULL, 5328958272ULL,
            "Yi-Coder-9B-Chat", "yi",
            "Q4_K_M", "Apache-2.0"
        },
        {
            "codegemma-7b-q4km", "7", "CodeGemma-7B Q4_K_M (~5 GiB)",
            "general-programming", "codegemma-7b-q4km", "codegemma-7b-Q4_K_M.gguf",
            "https://huggingface.co/bartowski/codegemma-7b-GGUF/resolve/7445262558e223ac999c8c34c6bcbb6e35821a47/codegemma-7b-Q4_K_M.gguf",
            "7445262558e223ac999c8c34c6bcbb6e35821a47",
            "5d36c9391069f7c54339a71a52ed8c0bb36219cba621189fc5427d4cdc6c8e5a",
            5120ULL, 7168ULL, 5329758592ULL,
            "CodeGemma-7B", "gemma",
            "Q4_K_M", "Gemma"
        },
        {
            "codellama-13b-q3km", "7", "CodeLlama-13B-Instruct Q3_K_M (~5.9 GiB)",
            "general-programming", "codellama-13b-q3km", "codellama-13b-instruct.Q3_K_M.gguf",
            "https://huggingface.co/TheBloke/CodeLlama-13B-Instruct-GGUF/resolve/82f1dd9567b9b20b7e8f8aa9ecf3d2f121e5d415/codellama-13b-instruct.Q3_K_M.gguf",
            "82f1dd9567b9b20b7e8f8aa9ecf3d2f121e5d415",
            "68357eb6af266639528c632483d86db554b1f3346dc9d2afc67702a1623b1a99",
            5632ULL, 7168ULL, 6337872256ULL,
            "", "",
            "", ""
        },
        {
            "codellama-13b-q4km", "8", "CodeLlama-13B-Instruct Q4_K_M (~7.3 GiB)",
            "general-programming", "codellama-13b-q4km", "codellama-13b-instruct.Q4_K_M.gguf",
            "https://huggingface.co/TheBloke/CodeLlama-13B-Instruct-GGUF/resolve/82f1dd9567b9b20b7e8f8aa9ecf3d2f121e5d415/codellama-13b-instruct.Q4_K_M.gguf",
            "82f1dd9567b9b20b7e8f8aa9ecf3d2f121e5d415",
            "48cc5600c5e35b1226208a53b1871f50efb15764232babaef23e2264c285d7d9",
            6144ULL, 8192ULL, 7866070016ULL,
            "", "",
            "", ""
        },
        {
            "deepseek-v2-lite-q3ks", "8", "DeepSeek-Coder-V2-Lite-Instruct Q3_K_S (~7 GiB)",
            "general-programming", "deepseek-v2-lite-q3ks", "DeepSeek-Coder-V2-Lite-Instruct-Q3_K_S.gguf",
            "https://huggingface.co/bartowski/DeepSeek-Coder-V2-Lite-Instruct-GGUF/resolve/8f248fa2072348f77a8bc37754e470de1f61866e/DeepSeek-Coder-V2-Lite-Instruct-Q3_K_S.gguf",
            "8f248fa2072348f77a8bc37754e470de1f61866e",
            "508e1ead6515d50a68d3d0f1e0d7f0c29f3ca351404703266793af6708ea89f5",
            6144ULL, 8192ULL, 7487663872ULL,
            "", "",
            "", ""
        },
        {
            "codellama-34b-q3ks", "16", "CodeLlama-34B-Instruct Q3_K_S (~13.6 GiB)",
            "general-programming", "codellama-34b-q3ks", "codellama-34b-instruct.Q3_K_S.gguf",
            "https://huggingface.co/TheBloke/CodeLlama-34B-Instruct-GGUF/resolve/7b84402c234acb1c5be542b5ecfc820ea3b74422/codellama-34b-instruct.Q3_K_S.gguf",
            "7b84402c234acb1c5be542b5ecfc820ea3b74422",
            "08b5aec470700ed1a703299d95dcf8ece296e99877ee99fbb040f09a79a2e4fa",
            12288ULL, 16384ULL, 14605349024ULL,
            "", "",
            "", ""
        },
        {
            "deepseek-v2-lite-q6k", "16", "DeepSeek-Coder-V2-Lite-Instruct Q6_K (~13.1 GiB)",
            "general-programming", "deepseek-v2-lite-q6k", "DeepSeek-Coder-V2-Lite-Instruct-Q6_K.gguf",
            "https://huggingface.co/bartowski/DeepSeek-Coder-V2-Lite-Instruct-GGUF/resolve/8f248fa2072348f77a8bc37754e470de1f61866e/DeepSeek-Coder-V2-Lite-Instruct-Q6_K.gguf",
            "8f248fa2072348f77a8bc37754e470de1f61866e",
            "1ff79f43ad5728d3179bf8fa7ee2993652f4306d6aeca9c35055f4f5b7b864cd",
            12288ULL, 16384ULL, 14066972416ULL,
            "", "",
            "", ""
        },
        {
            "codellama-34b-q4km", "24", "CodeLlama-34B-Instruct Q4_K_M (~18.8 GiB)",
            "general-programming", "codellama-34b-q4km", "codellama-34b-instruct.Q4_K_M.gguf",
            "https://huggingface.co/TheBloke/CodeLlama-34B-Instruct-GGUF/resolve/7b84402c234acb1c5be542b5ecfc820ea3b74422/codellama-34b-instruct.Q4_K_M.gguf",
            "7b84402c234acb1c5be542b5ecfc820ea3b74422",
            "57290fe55636910ab11b935dbe675d19781d06bd8020594d9135e06477e3c2bf",
            18432ULL, 24576ULL, 20219900064ULL,
            "", "",
            "", ""
        },
        {
            "gpt-oss-20b-q4km", "16", "gpt-oss-20b Q4_K_M (~10.8 GiB)",
            "general-programming", "gpt-oss-20b-q4km", "gpt-oss-20b-Q4_K_M.gguf",
            "https://huggingface.co/unsloth/gpt-oss-20b-GGUF/resolve/d449b42d93e1c2c7bda5312f5c25c8fb91dfa9b4/gpt-oss-20b-Q4_K_M.gguf",
            "d449b42d93e1c2c7bda5312f5c25c8fb91dfa9b4",
            "c27536640e410032865dc68781d80a08b98f8db5e93575919af8ccc0568aeb4f",
            11264ULL, 14336ULL, 11624759488ULL,
            "gpt-oss-20b", "gpt-oss",
            "Q4_K_M", "Apache-2.0"
        },
        {
            "gpt-oss-20b-q8", "16", "gpt-oss-20b Q8_0 (~11.3 GiB)",
            "general-programming", "gpt-oss-20b-q8", "gpt-oss-20b-Q8_0.gguf",
            "https://huggingface.co/unsloth/gpt-oss-20b-GGUF/resolve/d449b42d93e1c2c7bda5312f5c25c8fb91dfa9b4/gpt-oss-20b-Q8_0.gguf",
            "d449b42d93e1c2c7bda5312f5c25c8fb91dfa9b4",
            "bcd455d4034ec02f71a875b46cb17df44a97911d7258291973be4d21f98329f3",
            12288ULL, 16384ULL, 12109567168ULL,
            "gpt-oss-20b", "gpt-oss",
            "Q8_0", "Apache-2.0"
        },
        {
            "starcoder2-3b-q4km-ms", "3", "StarCoder2-3B Q4_K_M via ModelScope (~1.7 GiB)",
            "general-programming", "starcoder2-3b-q4km-ms", "starcoder2-3b-Q4_K_M.gguf",
            "https://modelscope.cn/models/second-state/StarCoder2-3B-GGUF/resolve/ad0cce4b6ae76131a5077a21ac72eee64bdb1a45/starcoder2-3b-Q4_K_M.gguf",
            "ad0cce4b6ae76131a5077a21ac72eee64bdb1a45",
            "d8fb39287a463549b80d97473b0a7595c3a5a6da3ae2604ca33906a1a43f7175",
            2048ULL, 3072ULL, 1848976448ULL,
            "", "",
            "", ""
        },
        {
            "phi35-mini-q4km-ms", "3", "Phi-3.5-mini-Instruct Q4_K_M via ModelScope (~2.2 GiB)",
            "general-programming", "phi35-mini-q4km-ms", "Phi-3.5-mini-instruct-Q4_K_M.gguf",
            "https://modelscope.cn/models/second-state/Phi-3.5-mini-instruct-GGUF/resolve/6240e2431eb84b0b091b3e226b5c78ce2a1086bc/Phi-3.5-mini-instruct-Q4_K_M.gguf",
            "6240e2431eb84b0b091b3e226b5c78ce2a1086bc",
            "c389ea28dc7f10dfbe30fc5e05452f832b1adf85989253ba590e3620b9584f06",
            2048ULL, 3072ULL, 2393232384ULL,
            "Phi-3.5-mini-Instruct", "phi3",
            "Q4_K_M", "MIT"
        },
        {
            "codegemma-7b-it-q4km-ms", "7", "CodeGemma-7B-it Q4_K_M via ModelScope (~5 GiB)",
            "general-programming", "codegemma-7b-it-q4km-ms", "codegemma-7b-it-Q4_K_M.gguf",
            "https://modelscope.cn/models/second-state/CodeGemma-7b-it-GGUF/resolve/5c22ebd36d051418d121946acd183d8b8d530e34/codegemma-7b-it-Q4_K_M.gguf",
            "5c22ebd36d051418d121946acd183d8b8d530e34",
            "7447e29e28f01ef593a4b0758cfb759a414ac32bbd65084509107e7091683fdc",
            5120ULL, 7168ULL, 5329759232ULL,
            "CodeGemma-7B-it", "gemma",
            "Q4_K_M", "Gemma"
        },
        {
            "deepseek-v2-lite-q4km-ms", "16", "DeepSeek-Coder-V2-Lite-Instruct Q4_K_M via ModelScope (~9.7 GiB)",
            "general-programming", "deepseek-v2-lite-q4km-ms", "DeepSeek-Coder-V2-Lite-Instruct-Q4_K_M.gguf",
            "https://modelscope.cn/models/second-state/DeepSeek-Coder-V2-Lite-Instruct-GGUF/resolve/fea4380e9006b556991fd088706b6c7ea69977d1/DeepSeek-Coder-V2-Lite-Instruct-Q4_K_M.gguf",
            "fea4380e9006b556991fd088706b6c7ea69977d1",
            "38bc76f3326b49b4d81d1027d092bf7ce5b4ed2de4136d1d2e7e6347c3ec8376",
            8192ULL, 10240ULL, 10364416480ULL,
            "", "",
            "", ""
        },
        {
            "codellama-13b-q4km-ms", "8", "CodeLlama-13B-Instruct Q4_K_M via ModelScope (~7.3 GiB)",
            "general-programming", "codellama-13b-q4km-ms", "CodeLlama-13b-Instruct-hf-Q4_K_M.gguf",
            "https://modelscope.cn/models/second-state/CodeLlama-13B-Instruct-GGUF/resolve/c1e2967a2531788fbbf5e6969ebaac55fec7fcae/CodeLlama-13b-Instruct-hf-Q4_K_M.gguf",
            "c1e2967a2531788fbbf5e6969ebaac55fec7fcae",
            "e2ad727d4893bc44add809e992c8f584e4fb1e986163a5b7510e2cc1f34b3c55",
            6144ULL, 8192ULL, 7866070080ULL,
            "", "",
            "", ""
        },
        {
            "qwen25-coder-0.5b-q4km", "1", "Qwen2.5-Coder-0.5B-Instruct Q4_K_M (~0.46 GiB)",
            "general-programming", "qwen25-coder-0.5b-q4km", "qwen2.5-coder-0.5b-instruct-q4_k_m.gguf",
            "https://huggingface.co/Qwen/Qwen2.5-Coder-0.5B-Instruct-GGUF/resolve/ebb2015119c907b064c512bf053e945850b5875f/qwen2.5-coder-0.5b-instruct-q4_k_m.gguf",
            "ebb2015119c907b064c512bf053e945850b5875f",
            "1d9614638d18024d0fbb36575a15f1302a3adf044df10345688ec4f6e1c4ff32",
            768ULL, 1024ULL, 491400064ULL,
            "Qwen2.5-Coder-0.5B-Instruct", "qwen2",
            "Q4_K_M", "Apache-2.0"
        },
        {
            "qwen25-coder-0.5b-q2k", "1", "Qwen2.5-Coder-0.5B-Instruct Q2_K (~0.4 GiB)",
            "general-programming", "qwen25-coder-0.5b-q2k", "qwen2.5-coder-0.5b-instruct-q2_k.gguf",
            "https://huggingface.co/Qwen/Qwen2.5-Coder-0.5B-Instruct-GGUF/resolve/ebb2015119c907b064c512bf053e945850b5875f/qwen2.5-coder-0.5b-instruct-q2_k.gguf",
            "ebb2015119c907b064c512bf053e945850b5875f",
            "f9bddf294ef15c80bb64a2cdcf15d5b25caf88fb4f4a12383bc9f7a01a09c2e3",
            448ULL, 640ULL, 415182720ULL,
            "Qwen2.5-Coder-0.5B-Instruct", "qwen2",
            "Q2_K", "Apache-2.0"
        },
        {
            "qwen25-coder-0.5b-q3km", "1", "Qwen2.5-Coder-0.5B-Instruct Q3_K_M (~0.4 GiB)",
            "general-programming", "qwen25-coder-0.5b-q3km", "qwen2.5-coder-0.5b-instruct-q3_k_m.gguf",
            "https://huggingface.co/Qwen/Qwen2.5-Coder-0.5B-Instruct-GGUF/resolve/ebb2015119c907b064c512bf053e945850b5875f/qwen2.5-coder-0.5b-instruct-q3_k_m.gguf",
            "ebb2015119c907b064c512bf053e945850b5875f",
            "dea21a75164dc20bf27fc983f3ef00cb99afb6f935311e19ef442a84deaa0a96",
            512ULL, 640ULL, 432041856ULL,
            "Qwen2.5-Coder-0.5B-Instruct", "qwen2",
            "Q3_K_M", "Apache-2.0"
        },
        {
            "qwen25-coder-0.5b-q8", "1", "Qwen2.5-Coder-0.5B-Instruct Q8_0 (~0.6 GiB)",
            "general-programming", "qwen25-coder-0.5b-q8", "qwen2.5-coder-0.5b-instruct-q8_0.gguf",
            "https://huggingface.co/Qwen/Qwen2.5-Coder-0.5B-Instruct-GGUF/resolve/ebb2015119c907b064c512bf053e945850b5875f/qwen2.5-coder-0.5b-instruct-q8_0.gguf",
            "ebb2015119c907b064c512bf053e945850b5875f",
            "e1a77721fa97d412f121878223eec81fb4ae6f271e18f922d746711f67b344d1",
            768ULL, 1024ULL, 675710848ULL,
            "Qwen2.5-Coder-0.5B-Instruct", "qwen2",
            "Q8_0", "Apache-2.0"
        },
        {
            "qwen25-1.5b-q2k", "2", "Qwen2.5-Coder-1.5B-Instruct Q2_K (~0.7 GiB)",
            "general-programming", "qwen25-1.5b-q2k", "qwen2.5-coder-1.5b-instruct-q2_k.gguf",
            "https://huggingface.co/Qwen/Qwen2.5-Coder-1.5B-Instruct-GGUF/resolve/f86cb2c1fa58255f8052cc32aeede1b7482d4361/qwen2.5-coder-1.5b-instruct-q2_k.gguf",
            "f86cb2c1fa58255f8052cc32aeede1b7482d4361",
            "3ec56d48cc5acdb93c4323f0d01a3b5db0c73c54fe71831199223720d37f6fcd",
            832ULL, 1152ULL, 752880192ULL,
            "Qwen2.5-Coder-1.5B-Instruct", "qwen2",
            "Q2_K", "Apache-2.0"
        },
        {
            "qwen25-1.5b-q3km", "2", "Qwen2.5-Coder-1.5B-Instruct Q3_K_M (~0.9 GiB)",
            "general-programming", "qwen25-1.5b-q3km", "qwen2.5-coder-1.5b-instruct-q3_k_m.gguf",
            "https://huggingface.co/Qwen/Qwen2.5-Coder-1.5B-Instruct-GGUF/resolve/f86cb2c1fa58255f8052cc32aeede1b7482d4361/qwen2.5-coder-1.5b-instruct-q3_k_m.gguf",
            "f86cb2c1fa58255f8052cc32aeede1b7482d4361",
            "d281a3a0010df03c8a0e3ffebd7f9444a95244fb518f132c5475e4b48d9adb5e",
            1024ULL, 1280ULL, 924456000ULL,
            "Qwen2.5-Coder-1.5B-Instruct", "qwen2",
            "Q3_K_M", "Apache-2.0"
        },
        {
            "qwen25-1.5b-q8", "3", "Qwen2.5-Coder-1.5B-Instruct Q8_0 (~1.8 GiB)",
            "general-programming", "qwen25-1.5b-q8", "qwen2.5-coder-1.5b-instruct-q8_0.gguf",
            "https://huggingface.co/Qwen/Qwen2.5-Coder-1.5B-Instruct-GGUF/resolve/f86cb2c1fa58255f8052cc32aeede1b7482d4361/qwen2.5-coder-1.5b-instruct-q8_0.gguf",
            "f86cb2c1fa58255f8052cc32aeede1b7482d4361",
            "507de59046601282ba768a9789900e6ccf60ed93ddf346730b7c68eb0715bc47",
            2048ULL, 2560ULL, 1894532160ULL,
            "Qwen2.5-Coder-1.5B-Instruct", "qwen2",
            "Q8_0", "Apache-2.0"
        },
        {
            "qwen25-3b-q2k", "2", "Qwen2.5-Coder-3B-Instruct Q2_K (~1.3 GiB)",
            "general-programming", "qwen25-3b-q2k", "qwen2.5-coder-3b-instruct-q2_k.gguf",
            "https://huggingface.co/Qwen/Qwen2.5-Coder-3B-Instruct-GGUF/resolve/f74adce6aa16316c625447af059dbebe4983757c/qwen2.5-coder-3b-instruct-q2_k.gguf",
            "f74adce6aa16316c625447af059dbebe4983757c",
            "cf5862615f10e7d19131ec8c3223c51ac43b63a66a0607b44db2b6943144d607",
            1472ULL, 1920ULL, 1376856512ULL,
            "Qwen2.5-Coder-3B-Instruct", "qwen2",
            "Q2_K", "Apache-2.0"
        },
        {
            "qwen25-3b-q3km", "3", "Qwen2.5-Coder-3B-Instruct Q3_K_M (~1.6 GiB)",
            "general-programming", "qwen25-3b-q3km", "qwen2.5-coder-3b-instruct-q3_k_m.gguf",
            "https://huggingface.co/Qwen/Qwen2.5-Coder-3B-Instruct-GGUF/resolve/f74adce6aa16316c625447af059dbebe4983757c/qwen2.5-coder-3b-instruct-q3_k_m.gguf",
            "f74adce6aa16316c625447af059dbebe4983757c",
            "fc3937db7dda9d9ef68ce1f63b5a84ac850ec3c07578461d645e5a88509348e3",
            1856ULL, 2432ULL, 1724178880ULL,
            "Qwen2.5-Coder-3B-Instruct", "qwen2",
            "Q3_K_M", "Apache-2.0"
        },
        {
            "qwen25-3b-q8", "5", "Qwen2.5-Coder-3B-Instruct Q8_0 (~3.4 GiB)",
            "general-programming", "qwen25-3b-q8", "qwen2.5-coder-3b-instruct-q8_0.gguf",
            "https://huggingface.co/Qwen/Qwen2.5-Coder-3B-Instruct-GGUF/resolve/f74adce6aa16316c625447af059dbebe4983757c/qwen2.5-coder-3b-instruct-q8_0.gguf",
            "f74adce6aa16316c625447af059dbebe4983757c",
            "f648c25dfd5a0870c4ad76724a745124ab5667ff97b664534fcbe46089b75ab8",
            3840ULL, 4864ULL, 3616088512ULL,
            "Qwen2.5-Coder-3B-Instruct", "qwen2",
            "Q8_0", "Apache-2.0"
        },
        {
            "qwen25-7b-q2k", "4", "Qwen2.5-Coder-7B-Instruct Q2_K (~2.8 GiB)",
            "general-programming", "qwen25-7b-q2k", "qwen2.5-coder-7b-instruct-q2_k.gguf",
            "https://huggingface.co/Qwen/Qwen2.5-Coder-7B-Instruct-GGUF/resolve/13fb94bfda8c8cf22497dc57b78f391a9acb426a/qwen2.5-coder-7b-instruct-q2_k.gguf",
            "13fb94bfda8c8cf22497dc57b78f391a9acb426a",
            "6ce2630974b0ef631e2b29064cd10a9cb16b278d001961d72bfd86987392c8d9",
            3200ULL, 4096ULL, 3015940032ULL,
            "Qwen2.5-Coder-7B-Instruct", "qwen2",
            "Q2_K", "Apache-2.0"
        },
        {
            "qwen25-7b-q3km", "5", "Qwen2.5-Coder-7B-Instruct Q3_K_M (~3.5 GiB)",
            "general-programming", "qwen25-7b-q3km", "qwen2.5-coder-7b-instruct-q3_k_m.gguf",
            "https://huggingface.co/Qwen/Qwen2.5-Coder-7B-Instruct-GGUF/resolve/13fb94bfda8c8cf22497dc57b78f391a9acb426a/qwen2.5-coder-7b-instruct-q3_k_m.gguf",
            "13fb94bfda8c8cf22497dc57b78f391a9acb426a",
            "ff5c64615cf8a44651d208e9d8da1f753feefc21605e6c1ee67957aa77257c3c",
            4032ULL, 5120ULL, 3808391104ULL,
            "Qwen2.5-Coder-7B-Instruct", "qwen2",
            "Q3_K_M", "Apache-2.0"
        },
        {
            "qwen25-7b-q8", "16", "Qwen2.5-Coder-7B-Instruct Q8_0 (~7.5 GiB)",
            "general-programming", "qwen25-7b-q8", "qwen2.5-coder-7b-instruct-q8_0.gguf",
            "https://huggingface.co/Qwen/Qwen2.5-Coder-7B-Instruct-GGUF/resolve/13fb94bfda8c8cf22497dc57b78f391a9acb426a/qwen2.5-coder-7b-instruct-q8_0.gguf",
            "13fb94bfda8c8cf22497dc57b78f391a9acb426a",
            "b36a4e1c3ddf2ba6fd5501b926128e5fc6430881caaff07e9629bd18f06f685f",
            8512ULL, 10752ULL, 8098525184ULL,
            "Qwen2.5-Coder-7B-Instruct", "qwen2",
            "Q8_0", "Apache-2.0"
        },
        {
            "qwen25-14b-q2k", "8", "Qwen2.5-Coder-14B-Instruct Q2_K (~5.4 GiB)",
            "general-programming", "qwen25-14b-q2k", "qwen2.5-coder-14b-instruct-q2_k.gguf",
            "https://huggingface.co/Qwen/Qwen2.5-Coder-14B-Instruct-GGUF/resolve/d0a692ef765eefbf2fabb130b3cb2e8917e3d225/qwen2.5-coder-14b-instruct-q2_k.gguf",
            "d0a692ef765eefbf2fabb130b3cb2e8917e3d225",
            "4542a8bb0c0ae6fb3dafeb305c61a8d6b2f894e96bb667d60cda105a1f313176",
            6080ULL, 7680ULL, 5770497408ULL,
            "Qwen2.5-Coder-14B-Instruct", "qwen2",
            "Q2_K", "Apache-2.0"
        },
        {
            "qwen25-14b-q3km", "16", "Qwen2.5-Coder-14B-Instruct Q3_K_M (~6.8 GiB)",
            "general-programming", "qwen25-14b-q3km", "qwen2.5-coder-14b-instruct-q3_k_m.gguf",
            "https://huggingface.co/Qwen/Qwen2.5-Coder-14B-Instruct-GGUF/resolve/d0a692ef765eefbf2fabb130b3cb2e8917e3d225/qwen2.5-coder-14b-instruct-q3_k_m.gguf",
            "d0a692ef765eefbf2fabb130b3cb2e8917e3d225",
            "93b69ebf2883dcd4d8fa0ebd4b4589ae0f3eedc721ac391a46d36d90762f4640",
            7744ULL, 9728ULL, 7339203968ULL,
            "Qwen2.5-Coder-14B-Instruct", "qwen2",
            "Q3_K_M", "Apache-2.0"
        },
        {
            "qwen25-14b-q8", "24", "Qwen2.5-Coder-14B-Instruct Q8_0 (~15 GiB)",
            "general-programming", "qwen25-14b-q8", "qwen2.5-coder-14b-instruct-q8_0.gguf",
            "https://huggingface.co/Qwen/Qwen2.5-Coder-14B-Instruct-GGUF/resolve/d0a692ef765eefbf2fabb130b3cb2e8917e3d225/qwen2.5-coder-14b-instruct-q8_0.gguf",
            "d0a692ef765eefbf2fabb130b3cb2e8917e3d225",
            "8180f66e3458e298830cebe7c9e8a9bde0ae6511d399fd99d3385b64324ac85d",
            16512ULL, 20736ULL, 15701597632ULL,
            "Qwen2.5-Coder-14B-Instruct", "qwen2",
            "Q8_0", "Apache-2.0"
        },
        {
            "qwen25-32b-q2k", "16", "Qwen2.5-Coder-32B-Instruct Q2_K (~11 GiB)",
            "general-programming", "qwen25-32b-q2k", "qwen2.5-coder-32b-instruct-q2_k.gguf",
            "https://huggingface.co/Qwen/Qwen2.5-Coder-32B-Instruct-GGUF/resolve/9d3053fce650fe1cdbdb75998c2a87add9d178ef/qwen2.5-coder-32b-instruct-q2_k.gguf",
            "9d3053fce650fe1cdbdb75998c2a87add9d178ef",
            "f52ef0a00588ef72ee23b8a40cb7a77143efad9786ac47a27ee318ded1a58f91",
            12928ULL, 16256ULL, 12313098432ULL,
            "Qwen2.5-Coder-32B-Instruct", "qwen2",
            "Q2_K", "Apache-2.0"
        },
        {
            "qwen25-32b-q3km", "24", "Qwen2.5-Coder-32B-Instruct Q3_K_M (~15 GiB)",
            "general-programming", "qwen25-32b-q3km", "qwen2.5-coder-32b-instruct-q3_k_m.gguf",
            "https://huggingface.co/Qwen/Qwen2.5-Coder-32B-Instruct-GGUF/resolve/9d3053fce650fe1cdbdb75998c2a87add9d178ef/qwen2.5-coder-32b-instruct-q3_k_m.gguf",
            "9d3053fce650fe1cdbdb75998c2a87add9d178ef",
            "bb2959545d91a8e9b8b25f3eae3bd5f4831cc59a71494bfab56d29f3fb2bd086",
            16768ULL, 20992ULL, 15935047872ULL,
            "Qwen2.5-Coder-32B-Instruct", "qwen2",
            "Q3_K_M", "Apache-2.0"
        },
        {
            "codegemma-2b-q2k", "2", "CodeGemma-2B Q2_K (~1.1 GiB)",
            "general-programming", "codegemma-2b-q2k", "codegemma-2b-Q2_K.gguf",
            "https://huggingface.co/bartowski/codegemma-2b-GGUF/resolve/4c371c2e831eebf3de95e32c841c8b38d4d9502e/codegemma-2b-Q2_K.gguf",
            "4c371c2e831eebf3de95e32c841c8b38d4d9502e",
            "1b7d4770626049df049be42ee507dca0040f3e7a669f11d7aa3f6d1ce9a4d3c6",
            1216ULL, 1536ULL, 1157923968ULL,
            "CodeGemma-2B", "gemma",
            "Q2_K", "Gemma"
        },
        {
            "codegemma-2b-q3km", "2", "CodeGemma-2B Q3_K_M (~1.3 GiB)",
            "general-programming", "codegemma-2b-q3km", "codegemma-2b-Q3_K_M.gguf",
            "https://huggingface.co/bartowski/codegemma-2b-GGUF/resolve/4c371c2e831eebf3de95e32c841c8b38d4d9502e/codegemma-2b-Q3_K_M.gguf",
            "4c371c2e831eebf3de95e32c841c8b38d4d9502e",
            "ba8fd65055d91b6b8722eb3181ec05bc803b8d89488aa473466983ab54331216",
            1472ULL, 1920ULL, 1383801984ULL,
            "CodeGemma-2B", "gemma",
            "Q3_K_M", "Gemma"
        },
        {
            "codegemma-2b-q8", "4", "CodeGemma-2B Q8_0 (~2.5 GiB)",
            "general-programming", "codegemma-2b-q8", "codegemma-2b-Q8_0.gguf",
            "https://huggingface.co/bartowski/codegemma-2b-GGUF/resolve/4c371c2e831eebf3de95e32c841c8b38d4d9502e/codegemma-2b-Q8_0.gguf",
            "4c371c2e831eebf3de95e32c841c8b38d4d9502e",
            "b2afa04a0561c7269016e4d5c0e0f80c2b09b2c166d999ada2b1956c30aaf621",
            2816ULL, 3584ULL, 2669069440ULL,
            "CodeGemma-2B", "gemma",
            "Q8_0", "Gemma"
        },
        {
            "starcoder2-3b-q2k", "2", "StarCoder2-3B Q2_K (~1.1 GiB)",
            "general-programming", "starcoder2-3b-q2k", "starcoder2-3b-Q2_K.gguf",
            "https://huggingface.co/second-state/StarCoder2-3B-GGUF/resolve/7fca3e2da2ce31df411461e2cb9cae2d2b492f35/starcoder2-3b-Q2_K.gguf",
            "7fca3e2da2ce31df411461e2cb9cae2d2b492f35",
            "d913d393f6af594bcae25cdc43b3962c59223adb364561143cf7931b9ce1b5a3",
            1216ULL, 1536ULL, 1149187136ULL,
            "", "",
            "", ""
        },
        {
            "starcoder2-3b-q3km", "2", "StarCoder2-3B Q3_K_M (~1.4 GiB)",
            "general-programming", "starcoder2-3b-q3km", "starcoder2-3b-Q3_K_M.gguf",
            "https://huggingface.co/second-state/StarCoder2-3B-GGUF/resolve/7fca3e2da2ce31df411461e2cb9cae2d2b492f35/starcoder2-3b-Q3_K_M.gguf",
            "7fca3e2da2ce31df411461e2cb9cae2d2b492f35",
            "150150764ef7a1a2f95815f5564525d7719445048c833acbc47ce6095d1cc51a",
            1600ULL, 2048ULL, 1513047104ULL,
            "", "",
            "", ""
        },
        {
            "starcoder2-3b-q8", "5", "StarCoder2-3B Q8_0 (~3.0 GiB)",
            "general-programming", "starcoder2-3b-q8", "starcoder2-3b-Q8_0.gguf",
            "https://huggingface.co/second-state/StarCoder2-3B-GGUF/resolve/7fca3e2da2ce31df411461e2cb9cae2d2b492f35/starcoder2-3b-Q8_0.gguf",
            "7fca3e2da2ce31df411461e2cb9cae2d2b492f35",
            "b4cbf961fa378b1a9f86d5930d3fcb0c31bacfd3ad64fcd27aca680b10059ff8",
            3392ULL, 4352ULL, 3224556608ULL,
            "", "",
            "", ""
        },
        {
            "phi35-mini-q2k", "2", "Phi-3.5-mini-Instruct Q2_K (~1.3 GiB)",
            "general-programming", "phi35-mini-q2k", "Phi-3.5-mini-instruct-Q2_K.gguf",
            "https://huggingface.co/bartowski/Phi-3.5-mini-instruct-GGUF/resolve/6d70da17e749a471ccb62ade694486011a75cda3/Phi-3.5-mini-instruct-Q2_K.gguf",
            "6d70da17e749a471ccb62ade694486011a75cda3",
            "7425cb5fec0d2673151c95d1418f5e11bdf102edd0d8b22986d17210adb05bbd",
            1536ULL, 1920ULL, 1416204576ULL,
            "Phi-3.5-mini-Instruct", "phi3",
            "Q2_K", "MIT"
        },
        {
            "phi35-mini-q3km", "3", "Phi-3.5-mini-Instruct Q3_K_M (~1.8 GiB)",
            "general-programming", "phi35-mini-q3km", "Phi-3.5-mini-instruct-Q3_K_M.gguf",
            "https://huggingface.co/bartowski/Phi-3.5-mini-instruct-GGUF/resolve/6d70da17e749a471ccb62ade694486011a75cda3/Phi-3.5-mini-instruct-Q3_K_M.gguf",
            "6d70da17e749a471ccb62ade694486011a75cda3",
            "5e005a663fc15ee51c9aea089fbd63a557458b020ee799e369d17da273a8b855",
            2112ULL, 2688ULL, 1955477280ULL,
            "Phi-3.5-mini-Instruct", "phi3",
            "Q3_K_M", "MIT"
        },
        {
            "phi35-mini-q8", "6", "Phi-3.5-mini-Instruct Q8_0 (~3.8 GiB)",
            "general-programming", "phi35-mini-q8", "Phi-3.5-mini-instruct-Q8_0.gguf",
            "https://huggingface.co/bartowski/Phi-3.5-mini-instruct-GGUF/resolve/6d70da17e749a471ccb62ade694486011a75cda3/Phi-3.5-mini-instruct-Q8_0.gguf",
            "6d70da17e749a471ccb62ade694486011a75cda3",
            "76fbf02f6fe92af57dbd818409bc8a0240026f1f3609bb405c3be94c973fb823",
            4288ULL, 5376ULL, 4061222688ULL,
            "Phi-3.5-mini-Instruct", "phi3",
            "Q8_0", "MIT"
        },
        {
            "deepseek-coder-1.3b-q2k", "1", "DeepSeek-Coder-1.3B-Instruct Q2_K (~0.6 GiB)",
            "general-programming", "deepseek-coder-1.3b-q2k", "deepseek-coder-1.3b-instruct.Q2_K.gguf",
            "https://huggingface.co/TheBloke/deepseek-coder-1.3b-instruct-GGUF/resolve/4595af8c3dff738094bd6c86054dfb5a90d5c41e/deepseek-coder-1.3b-instruct.Q2_K.gguf",
            "4595af8c3dff738094bd6c86054dfb5a90d5c41e",
            "80648071361b5d0d71457bfabf71c40d7475e1792a988bc7c64f5598ab20e4c1",
            704ULL, 896ULL, 631705632ULL,
            "", "",
            "", ""
        },
        {
            "deepseek-coder-1.3b-q3km", "1", "DeepSeek-Coder-1.3B-Instruct Q3_K_M (~0.7 GiB)",
            "general-programming", "deepseek-coder-1.3b-q3km", "deepseek-coder-1.3b-instruct.Q3_K_M.gguf",
            "https://huggingface.co/TheBloke/deepseek-coder-1.3b-instruct-GGUF/resolve/4595af8c3dff738094bd6c86054dfb5a90d5c41e/deepseek-coder-1.3b-instruct.Q3_K_M.gguf",
            "4595af8c3dff738094bd6c86054dfb5a90d5c41e",
            "ff02a88557469e4ea95500f7a845562bb8b670f119809ec9e71baaab5ed84db6",
            768ULL, 1024ULL, 704966688ULL,
            "", "",
            "", ""
        },
        {
            "deepseek-coder-1.3b-q8", "2", "DeepSeek-Coder-1.3B-Instruct Q8_0 (~1.3 GiB)",
            "general-programming", "deepseek-coder-1.3b-q8", "deepseek-coder-1.3b-instruct.Q8_0.gguf",
            "https://huggingface.co/TheBloke/deepseek-coder-1.3b-instruct-GGUF/resolve/4595af8c3dff738094bd6c86054dfb5a90d5c41e/deepseek-coder-1.3b-instruct.Q8_0.gguf",
            "4595af8c3dff738094bd6c86054dfb5a90d5c41e",
            "36eb025121a50ee6d37fe900659393ff8fb5ea34adc0e3c11fc635e07624dcdb",
            1536ULL, 1920ULL, 1432219680ULL,
            "", "",
            "", ""
        },
        {
            "deepseek-coder-6.7b-q2k", "4", "DeepSeek-Coder-6.7B-Instruct Q2_K (~2.6 GiB)",
            "general-programming", "deepseek-coder-6.7b-q2k", "deepseek-coder-6.7b-instruct.Q2_K.gguf",
            "https://huggingface.co/TheBloke/deepseek-coder-6.7B-instruct-GGUF/resolve/9e221e6b41cb1bf1c5d8f9718e81e3dc781f7557/deepseek-coder-6.7b-instruct.Q2_K.gguf",
            "9e221e6b41cb1bf1c5d8f9718e81e3dc781f7557",
            "56447b37bd7c606c081d9b216b69c6cd77db6b473721b709a7b21ed4606e4960",
            3008ULL, 3840ULL, 2827706592ULL,
            "", "",
            "", ""
        },
        {
            "deepseek-coder-6.7b-q3km", "5", "DeepSeek-Coder-6.7B-Instruct Q3_K_M (~3.1 GiB)",
            "general-programming", "deepseek-coder-6.7b-q3km", "deepseek-coder-6.7b-instruct.Q3_K_M.gguf",
            "https://huggingface.co/TheBloke/deepseek-coder-6.7B-instruct-GGUF/resolve/9e221e6b41cb1bf1c5d8f9718e81e3dc781f7557/deepseek-coder-6.7b-instruct.Q3_K_M.gguf",
            "9e221e6b41cb1bf1c5d8f9718e81e3dc781f7557",
            "6c841fde94b9103c92e875c9ab0ebeeb624b0709b2410c3bd942c80cd635117f",
            3520ULL, 4480ULL, 3299877088ULL,
            "", "",
            "", ""
        },
        {
            "deepseek-coder-6.7b-q8", "16", "DeepSeek-Coder-6.7B-Instruct Q8_0 (~6.7 GiB)",
            "general-programming", "deepseek-coder-6.7b-q8", "deepseek-coder-6.7b-instruct.Q8_0.gguf",
            "https://huggingface.co/TheBloke/deepseek-coder-6.7B-instruct-GGUF/resolve/9e221e6b41cb1bf1c5d8f9718e81e3dc781f7557/deepseek-coder-6.7b-instruct.Q8_0.gguf",
            "9e221e6b41cb1bf1c5d8f9718e81e3dc781f7557",
            "02cd6ce7ccec670cf6d3dd147932f13e584f9e964d5a3297a74b401b658471ae",
            7552ULL, 9472ULL, 7163879648ULL,
            "", "",
            "", ""
        },
        {
            "codellama-7b-q2k", "4", "CodeLlama-7B-Instruct Q2_K (~2.6 GiB)",
            "general-programming", "codellama-7b-q2k", "codellama-7b-instruct.Q2_K.gguf",
            "https://huggingface.co/TheBloke/CodeLlama-7B-Instruct-GGUF/resolve/2f064ee0c6ae3f025ec4e392c6ba5dd049c77969/codellama-7b-instruct.Q2_K.gguf",
            "2f064ee0c6ae3f025ec4e392c6ba5dd049c77969",
            "73b298f722a8d9d789508022c2adda18f3050942a4767923162b95b0bc65b9ad",
            3008ULL, 3840ULL, 2826016448ULL,
            "", "",
            "", ""
        },
        {
            "codellama-7b-q3km", "5", "CodeLlama-7B-Instruct Q3_K_M (~3.1 GiB)",
            "general-programming", "codellama-7b-q3km", "codellama-7b-instruct.Q3_K_M.gguf",
            "https://huggingface.co/TheBloke/CodeLlama-7B-Instruct-GGUF/resolve/2f064ee0c6ae3f025ec4e392c6ba5dd049c77969/codellama-7b-instruct.Q3_K_M.gguf",
            "2f064ee0c6ae3f025ec4e392c6ba5dd049c77969",
            "b228769a9027e27b2098be6ca108a0b9867bc45c51f2ede8309c21e827a3ed71",
            3520ULL, 4480ULL, 3298087104ULL,
            "", "",
            "", ""
        },
        {
            "codellama-7b-q8", "16", "CodeLlama-7B-Instruct Q8_0 (~6.7 GiB)",
            "general-programming", "codellama-7b-q8", "codellama-7b-instruct.Q8_0.gguf",
            "https://huggingface.co/TheBloke/CodeLlama-7B-Instruct-GGUF/resolve/2f064ee0c6ae3f025ec4e392c6ba5dd049c77969/codellama-7b-instruct.Q8_0.gguf",
            "2f064ee0c6ae3f025ec4e392c6ba5dd049c77969",
            "2126a5b9e8576ebea8889792ec5e459423935daae17ac0ffdfbedb39d222a20e",
            7552ULL, 9472ULL, 7161229504ULL,
            "", "",
            "", ""
        },
        {
            "starcoder2-7b-q2k", "4", "StarCoder2-7B Q2_K (~2.6 GiB)",
            "general-programming", "starcoder2-7b-q2k", "starcoder2-7b.Q2_K.gguf",
            "https://huggingface.co/QuantFactory/starcoder2-7b-GGUF/resolve/673788033750e9a7a677072a5184e3c923c19355/starcoder2-7b.Q2_K.gguf",
            "673788033750e9a7a677072a5184e3c923c19355",
            "cee1f6c4e5c99b727fa017f2d250c5004d1f6fa92714180be3d6177a6ee0ba3d",
            3008ULL, 3840ULL, 2836020352ULL,
            "", "",
            "", ""
        },
        {
            "starcoder2-7b-q3km", "5", "StarCoder2-7B Q3_K_M (~3.4 GiB)",
            "general-programming", "starcoder2-7b-q3km", "starcoder2-7b.Q3_K_M.gguf",
            "https://huggingface.co/QuantFactory/starcoder2-7b-GGUF/resolve/673788033750e9a7a677072a5184e3c923c19355/starcoder2-7b.Q3_K_M.gguf",
            "673788033750e9a7a677072a5184e3c923c19355",
            "12703bd7dab43b7f814bdcfd5a78910e711cfae85afba9e57f4d376cbde552ee",
            3904ULL, 4992ULL, 3661773952ULL,
            "", "",
            "", ""
        },
        {
            "starcoder2-7b-q8", "16", "StarCoder2-7B Q8_0 (~7.1 GiB)",
            "general-programming", "starcoder2-7b-q8", "starcoder2-7b.Q8_0.gguf",
            "https://huggingface.co/QuantFactory/starcoder2-7b-GGUF/resolve/673788033750e9a7a677072a5184e3c923c19355/starcoder2-7b.Q8_0.gguf",
            "673788033750e9a7a677072a5184e3c923c19355",
            "125fa7d84bedba325470301678beae85aedadfc7b42b5214c864a7e01fa22166",
            8064ULL, 10112ULL, 7628930176ULL,
            "", "",
            "", ""
        },
        {
            "yi-coder-9b-q2k", "5", "Yi-Coder-9B-Chat Q2_K (~3.1 GiB)",
            "general-programming", "yi-coder-9b-q2k", "Yi-Coder-9B-Chat-Q2_K.gguf",
            "https://huggingface.co/bartowski/Yi-Coder-9B-Chat-GGUF/resolve/2f28bfc370a5457310f3880202b2ed577e2bcbd8/Yi-Coder-9B-Chat-Q2_K.gguf",
            "2f28bfc370a5457310f3880202b2ed577e2bcbd8",
            "06f0a92a4462ecc8ffb97d6f810402e283cd8336a07965843d7ba7cda5c4dce1",
            3520ULL, 4480ULL, 3354325824ULL,
            "Yi-Coder-9B-Chat", "yi",
            "Q2_K", "Apache-2.0"
        },
        {
            "yi-coder-9b-q3km", "6", "Yi-Coder-9B-Chat Q3_K_M (~4.0 GiB)",
            "general-programming", "yi-coder-9b-q3km", "Yi-Coder-9B-Chat-Q3_K_M.gguf",
            "https://huggingface.co/bartowski/Yi-Coder-9B-Chat-GGUF/resolve/2f28bfc370a5457310f3880202b2ed577e2bcbd8/Yi-Coder-9B-Chat-Q3_K_M.gguf",
            "2f28bfc370a5457310f3880202b2ed577e2bcbd8",
            "5f1e9b4772c4ff684b1b6ddb9d48d9d02313ecde7834acd38f95b15dfb291c8f",
            4544ULL, 5760ULL, 4324406080ULL,
            "Yi-Coder-9B-Chat", "yi",
            "Q3_K_M", "Apache-2.0"
        },
        {
            "yi-coder-9b-q8", "16", "Yi-Coder-9B-Chat Q8_0 (~8.7 GiB)",
            "general-programming", "yi-coder-9b-q8", "Yi-Coder-9B-Chat-Q8_0.gguf",
            "https://huggingface.co/bartowski/Yi-Coder-9B-Chat-GGUF/resolve/2f28bfc370a5457310f3880202b2ed577e2bcbd8/Yi-Coder-9B-Chat-Q8_0.gguf",
            "2f28bfc370a5457310f3880202b2ed577e2bcbd8",
            "92b56ba08111e0b7f99337a6966308426c871754d2d2fa57aafbe07221145842",
            9856ULL, 12416ULL, 9383916352ULL,
            "Yi-Coder-9B-Chat", "yi",
            "Q8_0", "Apache-2.0"
        },
        {
            "codegemma-7b-q2k", "5", "CodeGemma-7B Q2_K (~3.2 GiB)",
            "general-programming", "codegemma-7b-q2k", "codegemma-7b-Q2_K.gguf",
            "https://huggingface.co/bartowski/codegemma-7b-GGUF/resolve/7445262558e223ac999c8c34c6bcbb6e35821a47/codegemma-7b-Q2_K.gguf",
            "7445262558e223ac999c8c34c6bcbb6e35821a47",
            "a3bbd1b2aa85dcb315c3e18fff0e3a470f22ad33810138ac0001fd750fcd7588",
            3712ULL, 4736ULL, 3481446784ULL,
            "CodeGemma-7B", "gemma",
            "Q2_K", "Gemma"
        },
        {
            "codegemma-7b-q3km", "6", "CodeGemma-7B Q3_K_M (~4.1 GiB)",
            "general-programming", "codegemma-7b-q3km", "codegemma-7b-Q3_K_M.gguf",
            "https://huggingface.co/bartowski/codegemma-7b-GGUF/resolve/7445262558e223ac999c8c34c6bcbb6e35821a47/codegemma-7b-Q3_K_M.gguf",
            "7445262558e223ac999c8c34c6bcbb6e35821a47",
            "eb65f44dffa132249940e4785aab30f05ac65175b6b65be33b1ac1b49eb60ef6",
            4608ULL, 5760ULL, 4369328512ULL,
            "CodeGemma-7B", "gemma",
            "Q3_K_M", "Gemma"
        },
        {
            "codegemma-7b-q8", "16", "CodeGemma-7B Q8_0 (~8.5 GiB)",
            "general-programming", "codegemma-7b-q8", "codegemma-7b-Q8_0.gguf",
            "https://huggingface.co/bartowski/codegemma-7b-GGUF/resolve/7445262558e223ac999c8c34c6bcbb6e35821a47/codegemma-7b-Q8_0.gguf",
            "7445262558e223ac999c8c34c6bcbb6e35821a47",
            "a5f7f8aec7cd475e86fd203bcf4bdc76c2df3172b3d2567656ae4e5360166a5b",
            9536ULL, 12032ULL, 9077844352ULL,
            "CodeGemma-7B", "gemma",
            "Q8_0", "Gemma"
        },
        {
            "codellama-13b-q2k", "7", "CodeLlama-13B-Instruct Q2_K (~5.1 GiB)",
            "general-programming", "codellama-13b-q2k", "codellama-13b-instruct.Q2_K.gguf",
            "https://huggingface.co/TheBloke/CodeLlama-13B-Instruct-GGUF/resolve/82f1dd9567b9b20b7e8f8aa9ecf3d2f121e5d415/codellama-13b-instruct.Q2_K.gguf",
            "82f1dd9567b9b20b7e8f8aa9ecf3d2f121e5d415",
            "93224011d01cce6b6bf13c1c3d7a90d0a3561b7ffc330b5e83eaf0c3f6a00acf",
            5696ULL, 7168ULL, 5429442816ULL,
            "", "",
            "", ""
        },
        {
            "codellama-13b-q8", "24", "CodeLlama-13B-Instruct Q8_0 (~13 GiB)",
            "general-programming", "codellama-13b-q8", "codellama-13b-instruct.Q8_0.gguf",
            "https://huggingface.co/TheBloke/CodeLlama-13B-Instruct-GGUF/resolve/82f1dd9567b9b20b7e8f8aa9ecf3d2f121e5d415/codellama-13b-instruct.Q8_0.gguf",
            "82f1dd9567b9b20b7e8f8aa9ecf3d2f121e5d415",
            "97c5a3aaae76ccbc69800cd2fd73ba6a94cd14a7d26d6be1d3c38163f449bdd6",
            14528ULL, 18176ULL, 13831494016ULL,
            "", "",
            "", ""
        },
        {
            "deepseek-v2-lite-q2k", "16", "DeepSeek-Coder-V2-Lite-Instruct Q2_K (~6.0 GiB)",
            "general-programming", "deepseek-v2-lite-q2k", "DeepSeek-Coder-V2-Lite-Instruct-Q2_K.gguf",
            "https://huggingface.co/bartowski/DeepSeek-Coder-V2-Lite-Instruct-GGUF/resolve/8f248fa2072348f77a8bc37754e470de1f61866e/DeepSeek-Coder-V2-Lite-Instruct-Q2_K.gguf",
            "8f248fa2072348f77a8bc37754e470de1f61866e",
            "5e2ac4b3477aa11ff460739ec326040ad07a3fc1c42da8e15b0465054879b5d3",
            6784ULL, 8576ULL, 6430464768ULL,
            "", "",
            "", ""
        },
        {
            "deepseek-v2-lite-q3km", "16", "DeepSeek-Coder-V2-Lite-Instruct Q3_K_M (~7.6 GiB)",
            "general-programming", "deepseek-v2-lite-q3km", "DeepSeek-Coder-V2-Lite-Instruct-Q3_K_M.gguf",
            "https://huggingface.co/bartowski/DeepSeek-Coder-V2-Lite-Instruct-GGUF/resolve/8f248fa2072348f77a8bc37754e470de1f61866e/DeepSeek-Coder-V2-Lite-Instruct-Q3_K_M.gguf",
            "8f248fa2072348f77a8bc37754e470de1f61866e",
            "30f78fce33d19c4bf68c410cd6504d424346dfbe846b4d3df5558e0be078a2a1",
            8576ULL, 10752ULL, 8126607104ULL,
            "", "",
            "", ""
        },
        {
            "deepseek-v2-lite-q8", "24", "DeepSeek-Coder-V2-Lite-Instruct Q8_0 (~16 GiB)",
            "general-programming", "deepseek-v2-lite-q8", "DeepSeek-Coder-V2-Lite-Instruct-Q8_0.gguf",
            "https://huggingface.co/bartowski/DeepSeek-Coder-V2-Lite-Instruct-GGUF/resolve/8f248fa2072348f77a8bc37754e470de1f61866e/DeepSeek-Coder-V2-Lite-Instruct-Q8_0.gguf",
            "8f248fa2072348f77a8bc37754e470de1f61866e",
            "af12f49d16dc54f2148af3f6dc3ee5de9461e8586a1d13e279b7e7623bf796fb",
            17536ULL, 22016ULL, 16702518016ULL,
            "", "",
            "", ""
        },
        {
            "codellama-34b-q2k", "24", "CodeLlama-34B-Instruct Q2_K (~13 GiB)",
            "general-programming", "codellama-34b-q2k", "codellama-34b-instruct.Q2_K.gguf",
            "https://huggingface.co/TheBloke/CodeLlama-34B-Instruct-GGUF/resolve/7b84402c234acb1c5be542b5ecfc820ea3b74422/codellama-34b-instruct.Q2_K.gguf",
            "7b84402c234acb1c5be542b5ecfc820ea3b74422",
            "773d24e9c1eb2cd71a9a2b6bf944cab4badb9616051a374ef19c757b58582016",
            14912ULL, 18688ULL, 14210674848ULL,
            "", "",
            "", ""
        },
        {
            "codellama-34b-q8", "64", "CodeLlama-34B-Instruct Q8_0 (~33 GiB)",
            "general-programming", "codellama-34b-q8", "codellama-34b-instruct.Q8_0.gguf",
            "https://huggingface.co/TheBloke/CodeLlama-34B-Instruct-GGUF/resolve/7b84402c234acb1c5be542b5ecfc820ea3b74422/codellama-34b-instruct.Q8_0.gguf",
            "7b84402c234acb1c5be542b5ecfc820ea3b74422",
            "c989409bf89c1e88a07a4fb9493527305770cd1692d233a74fdd8d647dcdc83b",
            37632ULL, 47104ULL, 35856052384ULL,
            "", "",
            "", ""
        },
        {
            "gpt-oss-20b-q2k", "16", "gpt-oss-20b Q2_K (~11 GiB)",
            "general-programming", "gpt-oss-20b-q2k", "gpt-oss-20b-Q2_K.gguf",
            "https://huggingface.co/unsloth/gpt-oss-20b-GGUF/resolve/d449b42d93e1c2c7bda5312f5c25c8fb91dfa9b4/gpt-oss-20b-Q2_K.gguf",
            "d449b42d93e1c2c7bda5312f5c25c8fb91dfa9b4",
            "129b8e3865517cbba55bbb7d4f0cc2444a014e73a619eafbac247ef6573a19ee",
            12032ULL, 15104ULL, 11468317888ULL,
            "gpt-oss-20b", "gpt-oss",
            "Q2_K", "Apache-2.0"
        },
        {
            "gpt-oss-20b-q3km", "16", "gpt-oss-20b Q3_K_M (~11 GiB)",
            "general-programming", "gpt-oss-20b-q3km", "gpt-oss-20b-Q3_K_M.gguf",
            "https://huggingface.co/unsloth/gpt-oss-20b-GGUF/resolve/d449b42d93e1c2c7bda5312f5c25c8fb91dfa9b4/gpt-oss-20b-Q3_K_M.gguf",
            "d449b42d93e1c2c7bda5312f5c25c8fb91dfa9b4",
            "bc02b57e05cfef36b1f6f4a952666a76b26838f8cf431412cb0cd19f41cf8040",
            12096ULL, 15232ULL, 11506103488ULL,
            "gpt-oss-20b", "gpt-oss",
            "Q3_K_M", "Apache-2.0"
        },
        {
            "starcoder2-3b-q2k-ms", "2", "StarCoder2-3B Q2_K (~1.1 GiB) via ModelScope",
            "general-programming", "starcoder2-3b-q2k-ms", "starcoder2-3b-Q2_K.gguf",
            "https://modelscope.cn/models/second-state/StarCoder2-3B-GGUF/resolve/ad0cce4b6ae76131a5077a21ac72eee64bdb1a45/starcoder2-3b-Q2_K.gguf",
            "ad0cce4b6ae76131a5077a21ac72eee64bdb1a45",
            "d913d393f6af594bcae25cdc43b3962c59223adb364561143cf7931b9ce1b5a3",
            1216ULL, 1536ULL, 1149187136ULL,
            "", "",
            "", ""
        },
        {
            "starcoder2-3b-q3km-ms", "2", "StarCoder2-3B Q3_K_M (~1.4 GiB) via ModelScope",
            "general-programming", "starcoder2-3b-q3km-ms", "starcoder2-3b-Q3_K_M.gguf",
            "https://modelscope.cn/models/second-state/StarCoder2-3B-GGUF/resolve/ad0cce4b6ae76131a5077a21ac72eee64bdb1a45/starcoder2-3b-Q3_K_M.gguf",
            "ad0cce4b6ae76131a5077a21ac72eee64bdb1a45",
            "150150764ef7a1a2f95815f5564525d7719445048c833acbc47ce6095d1cc51a",
            1600ULL, 2048ULL, 1513047104ULL,
            "", "",
            "", ""
        },
        {
            "starcoder2-3b-q8-ms", "5", "StarCoder2-3B Q8_0 (~3.0 GiB) via ModelScope",
            "general-programming", "starcoder2-3b-q8-ms", "starcoder2-3b-Q8_0.gguf",
            "https://modelscope.cn/models/second-state/StarCoder2-3B-GGUF/resolve/ad0cce4b6ae76131a5077a21ac72eee64bdb1a45/starcoder2-3b-Q8_0.gguf",
            "ad0cce4b6ae76131a5077a21ac72eee64bdb1a45",
            "b4cbf961fa378b1a9f86d5930d3fcb0c31bacfd3ad64fcd27aca680b10059ff8",
            3392ULL, 4352ULL, 3224556608ULL,
            "", "",
            "", ""
        },
        {
            "phi35-mini-q2k-ms", "2", "Phi-3.5-mini-Instruct Q2_K (~1.3 GiB) via ModelScope",
            "general-programming", "phi35-mini-q2k-ms", "Phi-3.5-mini-instruct-Q2_K.gguf",
            "https://modelscope.cn/models/second-state/Phi-3.5-mini-instruct-GGUF/resolve/6240e2431eb84b0b091b3e226b5c78ce2a1086bc/Phi-3.5-mini-instruct-Q2_K.gguf",
            "6240e2431eb84b0b091b3e226b5c78ce2a1086bc",
            "2e6564bea11e9447560fd9a83acac1b248293c2466ffee97683d3bf2288ce8e9",
            1536ULL, 1920ULL, 1416204288ULL,
            "Phi-3.5-mini-Instruct", "phi3",
            "Q2_K", "MIT"
        },
        {
            "phi35-mini-q3km-ms", "3", "Phi-3.5-mini-Instruct Q3_K_M (~1.8 GiB) via ModelScope",
            "general-programming", "phi35-mini-q3km-ms", "Phi-3.5-mini-instruct-Q3_K_M.gguf",
            "https://modelscope.cn/models/second-state/Phi-3.5-mini-instruct-GGUF/resolve/6240e2431eb84b0b091b3e226b5c78ce2a1086bc/Phi-3.5-mini-instruct-Q3_K_M.gguf",
            "6240e2431eb84b0b091b3e226b5c78ce2a1086bc",
            "e090e1ea535e89f4f18737dbf1c79bb5a168990159e9d8a6174374fe921096ca",
            2112ULL, 2688ULL, 1955476992ULL,
            "Phi-3.5-mini-Instruct", "phi3",
            "Q3_K_M", "MIT"
        },
        {
            "phi35-mini-q8-ms", "6", "Phi-3.5-mini-Instruct Q8_0 (~3.8 GiB) via ModelScope",
            "general-programming", "phi35-mini-q8-ms", "Phi-3.5-mini-instruct-Q8_0.gguf",
            "https://modelscope.cn/models/second-state/Phi-3.5-mini-instruct-GGUF/resolve/6240e2431eb84b0b091b3e226b5c78ce2a1086bc/Phi-3.5-mini-instruct-Q8_0.gguf",
            "6240e2431eb84b0b091b3e226b5c78ce2a1086bc",
            "81fd0b829a6ff15b3df3cc792de3f3e75a9fb2f1f55c0059af55a698b1173150",
            4288ULL, 5376ULL, 4061222400ULL,
            "Phi-3.5-mini-Instruct", "phi3",
            "Q8_0", "MIT"
        },
        {
            "codegemma-7b-it-q2k-ms", "5", "CodeGemma-7B-it Q2_K (~3.2 GiB) via ModelScope",
            "general-programming", "codegemma-7b-it-q2k-ms", "codegemma-7b-it-Q2_K.gguf",
            "https://modelscope.cn/models/second-state/CodeGemma-7b-it-GGUF/resolve/5c22ebd36d051418d121946acd183d8b8d530e34/codegemma-7b-it-Q2_K.gguf",
            "5c22ebd36d051418d121946acd183d8b8d530e34",
            "d88a283ce8d9cc4c22a9ccd562d629975b38f1a71a09cc936451a132d3b1590a",
            3712ULL, 4736ULL, 3481447424ULL,
            "CodeGemma-7B-it", "gemma",
            "Q2_K", "Gemma"
        },
        {
            "codegemma-7b-it-q3km-ms", "6", "CodeGemma-7B-it Q3_K_M (~4.1 GiB) via ModelScope",
            "general-programming", "codegemma-7b-it-q3km-ms", "codegemma-7b-it-Q3_K_M.gguf",
            "https://modelscope.cn/models/second-state/CodeGemma-7b-it-GGUF/resolve/5c22ebd36d051418d121946acd183d8b8d530e34/codegemma-7b-it-Q3_K_M.gguf",
            "5c22ebd36d051418d121946acd183d8b8d530e34",
            "66a621cbe70cae44427a474e4351cd920ccc2a2f7be081e23404712153bca589",
            4608ULL, 5760ULL, 4369329152ULL,
            "CodeGemma-7B-it", "gemma",
            "Q3_K_M", "Gemma"
        },
        {
            "codegemma-7b-it-q8-ms", "16", "CodeGemma-7B-it Q8_0 (~8.5 GiB) via ModelScope",
            "general-programming", "codegemma-7b-it-q8-ms", "codegemma-7b-it-Q8_0.gguf",
            "https://modelscope.cn/models/second-state/CodeGemma-7b-it-GGUF/resolve/5c22ebd36d051418d121946acd183d8b8d530e34/codegemma-7b-it-Q8_0.gguf",
            "5c22ebd36d051418d121946acd183d8b8d530e34",
            "20b20ee7b4265a5872bd58d669c394206f50418f3523b47563c9b1d4a78f37cb",
            9536ULL, 12032ULL, 9077844992ULL,
            "CodeGemma-7B-it", "gemma",
            "Q8_0", "Gemma"
        },
        {
            "deepseek-v2-lite-q2k-ms", "16", "DeepSeek-Coder-V2-Lite-Instruct Q2_K (~6.0 GiB) via ModelScope",
            "general-programming", "deepseek-v2-lite-q2k-ms", "DeepSeek-Coder-V2-Lite-Instruct-Q2_K.gguf",
            "https://modelscope.cn/models/second-state/DeepSeek-Coder-V2-Lite-Instruct-GGUF/resolve/fea4380e9006b556991fd088706b6c7ea69977d1/DeepSeek-Coder-V2-Lite-Instruct-Q2_K.gguf",
            "fea4380e9006b556991fd088706b6c7ea69977d1",
            "d4eb2f3365ac103d4231352b331577135be67931cecedad474c38e4cecb9182d",
            6784ULL, 8576ULL, 6430464480ULL,
            "", "",
            "", ""
        },
        {
            "deepseek-v2-lite-q3km-ms", "16", "DeepSeek-Coder-V2-Lite-Instruct Q3_K_M (~7.6 GiB) via ModelScope",
            "general-programming", "deepseek-v2-lite-q3km-ms", "DeepSeek-Coder-V2-Lite-Instruct-Q3_K_M.gguf",
            "https://modelscope.cn/models/second-state/DeepSeek-Coder-V2-Lite-Instruct-GGUF/resolve/fea4380e9006b556991fd088706b6c7ea69977d1/DeepSeek-Coder-V2-Lite-Instruct-Q3_K_M.gguf",
            "fea4380e9006b556991fd088706b6c7ea69977d1",
            "3918c980cc193aa63733db160fdd26ef3a668fecd5219d0e1aa62d12c86cc91f",
            8576ULL, 10752ULL, 8126606816ULL,
            "", "",
            "", ""
        },
        {
            "deepseek-v2-lite-q8-ms", "24", "DeepSeek-Coder-V2-Lite-Instruct Q8_0 (~16 GiB) via ModelScope",
            "general-programming", "deepseek-v2-lite-q8-ms", "DeepSeek-Coder-V2-Lite-Instruct-Q8_0.gguf",
            "https://modelscope.cn/models/second-state/DeepSeek-Coder-V2-Lite-Instruct-GGUF/resolve/fea4380e9006b556991fd088706b6c7ea69977d1/DeepSeek-Coder-V2-Lite-Instruct-Q8_0.gguf",
            "fea4380e9006b556991fd088706b6c7ea69977d1",
            "373dcfc92e01372709b6164fc836f677a6280e25e9eac5c434c64223207bfc4f",
            17536ULL, 22016ULL, 16702517728ULL,
            "", "",
            "", ""
        },
        {
            "codellama-13b-q2k-ms", "7", "CodeLlama-13B-Instruct Q2_K (~5.1 GiB) via ModelScope",
            "general-programming", "codellama-13b-q2k-ms", "CodeLlama-13b-Instruct-hf-Q2_K.gguf",
            "https://modelscope.cn/models/second-state/CodeLlama-13B-Instruct-GGUF/resolve/c1e2967a2531788fbbf5e6969ebaac55fec7fcae/CodeLlama-13b-Instruct-hf-Q2_K.gguf",
            "c1e2967a2531788fbbf5e6969ebaac55fec7fcae",
            "6dfc9309d48fffd2ecbdc0571f96a99da7d7bf094208a713e9a4cb8205546f29",
            5696ULL, 7168ULL, 5429442880ULL,
            "", "",
            "", ""
        },
        {
            "codellama-13b-q3km-ms", "16", "CodeLlama-13B-Instruct Q3_K_M (~5.9 GiB) via ModelScope",
            "general-programming", "codellama-13b-q3km-ms", "CodeLlama-13b-Instruct-hf-Q3_K_M.gguf",
            "https://modelscope.cn/models/second-state/CodeLlama-13B-Instruct-GGUF/resolve/c1e2967a2531788fbbf5e6969ebaac55fec7fcae/CodeLlama-13b-Instruct-hf-Q3_K_M.gguf",
            "c1e2967a2531788fbbf5e6969ebaac55fec7fcae",
            "d62f4656f12c2cc6763c9ea47fc1474dbaa5ab6adee4f3452f91da979fdbbabf",
            6656ULL, 8320ULL, 6337872320ULL,
            "", "",
            "", ""
        },
        {
            "codellama-13b-q8-ms", "24", "CodeLlama-13B-Instruct Q8_0 (~13 GiB) via ModelScope",
            "general-programming", "codellama-13b-q8-ms", "CodeLlama-13b-Instruct-hf-Q8_0.gguf",
            "https://modelscope.cn/models/second-state/CodeLlama-13B-Instruct-GGUF/resolve/c1e2967a2531788fbbf5e6969ebaac55fec7fcae/CodeLlama-13b-Instruct-hf-Q8_0.gguf",
            "c1e2967a2531788fbbf5e6969ebaac55fec7fcae",
            "44a69ac2d210548b1edc0f0b622a2c196e364d5923f695327c3b506f8581eb95",
            14528ULL, 18176ULL, 13831494080ULL,
            "", "",
            "", ""
        },
        {
            "qwen25-1.5b-instruct-q4km", "2", "Qwen2.5-1.5B-Instruct Q4_K_M (~1.0 GiB)",
            "conversation", "qwen25-1.5b-instruct-q4km", "qwen2.5-1.5b-instruct-q4_k_m.gguf",
            "https://huggingface.co/Qwen/Qwen2.5-1.5B-Instruct-GGUF/resolve/91cad51170dc346986eccefdc2dd33a9da36ead9/qwen2.5-1.5b-instruct-q4_k_m.gguf",
            "91cad51170dc346986eccefdc2dd33a9da36ead9",
            "6a1a2eb6d15622bf3c96857206351ba97e1af16c30d7a74ee38970e434e9407e",
            1536ULL, 2048ULL, 1117320736ULL,
            "Qwen2.5-1.5B-Instruct", "qwen2",
            "Q4_K_M", "Apache-2.0"
        },
        {
            "llama32-3b-instruct-q4km", "4", "Llama-3.2-3B-Instruct Q4_K_M (~1.9 GiB)",
            "conversation", "llama32-3b-instruct-q4km", "Llama-3.2-3B-Instruct-Q4_K_M.gguf",
            "https://huggingface.co/bartowski/Llama-3.2-3B-Instruct-GGUF/resolve/5ab33fa94d1d04e903623ae72c95d1696f09f9e8/Llama-3.2-3B-Instruct-Q4_K_M.gguf",
            "5ab33fa94d1d04e903623ae72c95d1696f09f9e8",
            "6c1a2b41161032677be168d354123594c0e6e67d2b9227c84f296ad037c728ff",
            3072ULL, 4096ULL, 2019377696ULL,
            "Llama-3.2-3B-Instruct", "llama",
            "Q4_K_M", "Llama-3.2"
        },
        {
            "llama31-8b-instruct-q4km", "8", "Meta-Llama-3.1-8B-Instruct Q4_K_M (~4.6 GiB)",
            "conversation", "llama31-8b-instruct-q4km", "Meta-Llama-3.1-8B-Instruct-Q4_K_M.gguf",
            "https://huggingface.co/bartowski/Meta-Llama-3.1-8B-Instruct-GGUF/resolve/bf5b95e96dac0462e2a09145ec66cae9a3f12067/Meta-Llama-3.1-8B-Instruct-Q4_K_M.gguf",
            "bf5b95e96dac0462e2a09145ec66cae9a3f12067",
            "7b064f5842bf9532c91456deda288a1b672397a54fa729aa665952863033557c",
            6144ULL, 8192ULL, 4920739232ULL,
            "Meta-Llama-3.1-8B-Instruct", "llama",
            "Q4_K_M", "Llama-3.1"
        },
        {
            "llama32-1b-instruct-q4km", "2", "Llama-3.2-1B-Instruct Q4_K_M (~0.75 GiB)",
            "conversation", "llama32-1b-instruct-q4km", "Llama-3.2-1B-Instruct-Q4_K_M.gguf",
            "https://huggingface.co/bartowski/Llama-3.2-1B-Instruct-GGUF/resolve/067b946cf014b7c697f3654f621d577a3e3afd1c/Llama-3.2-1B-Instruct-Q4_K_M.gguf",
            "067b946cf014b7c697f3654f621d577a3e3afd1c",
            "6f85a640a97cf2bf5b8e764087b1e83da0fdb51d7c9fab7d0fece9385611df83",
            1024ULL, 1536ULL, 807694464ULL,
            "Llama-3.2-1B-Instruct", "llama",
            "Q4_K_M", "Llama-3.2"
        },
        {
            "qwen25-3b-instruct-q4km", "4", "Qwen2.5-3B-Instruct Q4_K_M (~1.96 GiB)",
            "conversation", "qwen25-3b-instruct-q4km", "qwen2.5-3b-instruct-q4_k_m.gguf",
            "https://huggingface.co/Qwen/Qwen2.5-3B-Instruct-GGUF/resolve/7dabda4d13d513e3e842b20f0d435c732f172cbe/qwen2.5-3b-instruct-q4_k_m.gguf",
            "7dabda4d13d513e3e842b20f0d435c732f172cbe",
            "626b4a6678b86442240e33df819e00132d3ba7dddfe1cdc4fbb18e0a9615c62d",
            3072ULL, 4096ULL, 2104932768ULL,
            "Qwen2.5-3B-Instruct", "qwen2",
            "Q4_K_M", "Apache-2.0"
        },
        {
            "qwen25-7b-instruct-q4km", "8", "Qwen2.5-7B-Instruct Q4_K_M (~4.36 GiB)",
            "conversation", "qwen25-7b-instruct-q4km", "Qwen2.5-7B-Instruct-Q4_K_M.gguf",
            "https://huggingface.co/bartowski/Qwen2.5-7B-Instruct-GGUF/resolve/8911e8a47f92bac19d6f5c64a2e2095bd2f7d031/Qwen2.5-7B-Instruct-Q4_K_M.gguf",
            "8911e8a47f92bac19d6f5c64a2e2095bd2f7d031",
            "65b8fcd92af6b4fefa935c625d1ac27ea29dcb6ee14589c55a8f115ceaaa1423",
            6144ULL, 8192ULL, 4683074240ULL,
            "Qwen2.5-7B-Instruct", "qwen2",
            "Q4_K_M", "Apache-2.0"
        },
        {
            "gemma2-2b-it-q4km", "3", "Gemma-2-2B-it Q4_K_M (~1.59 GiB)",
            "conversation", "gemma2-2b-it-q4km", "gemma-2-2b-it-Q4_K_M.gguf",
            "https://huggingface.co/bartowski/gemma-2-2b-it-GGUF/resolve/855f67caed130e1befc571b52bd181be2e858883/gemma-2-2b-it-Q4_K_M.gguf",
            "855f67caed130e1befc571b52bd181be2e858883",
            "e0aee85060f168f0f2d8473d7ea41ce2f3230c1bc1374847505ea599288a7787",
            2048ULL, 3072ULL, 1708582752ULL,
            "Gemma-2-2B-it", "gemma",
            "Q4_K_M", "Gemma"
        },
        {
            "mistral-7b-instruct-v03-q4km", "8", "Mistral-7B-Instruct-v0.3 Q4_K_M (~4.07 GiB)",
            "conversation", "mistral-7b-instruct-v03-q4km", "Mistral-7B-Instruct-v0.3-Q4_K_M.gguf",
            "https://huggingface.co/bartowski/Mistral-7B-Instruct-v0.3-GGUF/resolve/61fd4167fff3ab01ee1cfe0da183fa27a944db48/Mistral-7B-Instruct-v0.3-Q4_K_M.gguf",
            "61fd4167fff3ab01ee1cfe0da183fa27a944db48",
            "1270d22c0fbb3d092fb725d4d96c457b7b687a5f5a715abe1e818da303e562b6",
            6144ULL, 8192ULL, 4372812000ULL,
            "Mistral-7B-Instruct-v0.3", "mistral",
            "Q4_K_M", "Apache-2.0"
        },
        {
            "granite31-2b-instruct-q4km", "3", "Granite-3.1-2B-Instruct Q4_K_M (~1.44 GiB)",
            "conversation", "granite31-2b-instruct-q4km", "granite-3.1-2b-instruct-Q4_K_M.gguf",
            "https://huggingface.co/bartowski/granite-3.1-2b-instruct-GGUF/resolve/e47b8b46c04cede00f9e19d5a846551b14b2efce/granite-3.1-2b-instruct-Q4_K_M.gguf",
            "e47b8b46c04cede00f9e19d5a846551b14b2efce",
            "774269c82fde2720ea18dcf457fb5bd028fe096139a0735f4ad59c0a270cfc9c",
            2048ULL, 3072ULL, 1545295424ULL,
            "Granite-3.1-2B-Instruct", "granite",
            "Q4_K_M", "Apache-2.0"
        },
        {
            "granite31-8b-instruct-q4km", "8", "Granite-3.1-8B-Instruct Q4_K_M (~4.6 GiB)",
            "conversation", "granite31-8b-instruct-q4km", "granite-3.1-8b-instruct-Q4_K_M.gguf",
            "https://huggingface.co/bartowski/granite-3.1-8b-instruct-GGUF/resolve/7a0f633d54069de889707a76353bc28b70361d9f/granite-3.1-8b-instruct-Q4_K_M.gguf",
            "7a0f633d54069de889707a76353bc28b70361d9f",
            "b72cfca8e30f23af77f922ce18d6fe1a5d4925907dddf7249c0cabc2739d48c8",
            6144ULL, 8192ULL, 4942858720ULL,
            "Granite-3.1-8B-Instruct", "granite",
            "Q4_K_M", "Apache-2.0"
        },
        {
            "olmo2-7b-instruct-q4km", "8", "OLMo-2-1124-7B-Instruct Q4_K_M (~4.16 GiB)",
            "conversation", "olmo2-7b-instruct-q4km", "OLMo-2-1124-7B-Instruct-Q4_K_M.gguf",
            "https://huggingface.co/bartowski/OLMo-2-1124-7B-Instruct-GGUF/resolve/01a56cca7da47f11851889af56ec36a9e75ceac8/OLMo-2-1124-7B-Instruct-Q4_K_M.gguf",
            "01a56cca7da47f11851889af56ec36a9e75ceac8",
            "88790198b8ab4f251b5b462756adc0265b3f6b9d9d87708d847645d9568bf168",
            6144ULL, 8192ULL, 4472020544ULL,
            "OLMo-2-1124-7B-Instruct", "olmo2",
            "Q4_K_M", "Apache-2.0"
        },
        {
            "smollm2-360m-instruct-q8", "1", "SmolLM2-360M-Instruct Q8_0 (~0.36 GiB)",
            "conversation", "smollm2-360m-instruct-q8", "smollm2-360m-instruct-q8_0.gguf",
            "https://huggingface.co/HuggingFaceTB/SmolLM2-360M-Instruct-GGUF/resolve/593b5a2e04c8f3e4ee880263f93e0bd2901ad47f/smollm2-360m-instruct-q8_0.gguf",
            "593b5a2e04c8f3e4ee880263f93e0bd2901ad47f",
            "48ab3034d0dd401fbc721eb1df3217902fee7dab9078992d66431f09b7750201",
            512ULL, 1024ULL, 386404992ULL,
            "SmolLM2-360M-Instruct", "llama",
            "Q8_0", "Apache-2.0"
        },
        {
            "smollm2-1.7b-instruct-q4km", "2", "SmolLM2-1.7B-Instruct Q4_K_M (~0.98 GiB)",
            "conversation", "smollm2-1.7b-instruct-q4km", "SmolLM2-1.7B-Instruct-Q4_K_M.gguf",
            "https://huggingface.co/unsloth/SmolLM2-1.7B-Instruct-GGUF/resolve/e933f1cdf73cc87cb67915bf5dd6ea81d36080ca/SmolLM2-1.7B-Instruct-Q4_K_M.gguf",
            "e933f1cdf73cc87cb67915bf5dd6ea81d36080ca",
            "61b6f90dd515fd3bffbd0f6ba716e87555dde77d9b0573a562c2c5e62afc4909",
            1536ULL, 2048ULL, 1055609504ULL,
            "SmolLM2-1.7B-Instruct", "llama",
            "Q4_K_M", "Apache-2.0"
        },
        {
            "tinyllama-1.1b-chat-q4km", "2", "TinyLlama-1.1B-Chat-v1.0 Q4_K_M (~0.62 GiB)",
            "conversation", "tinyllama-1.1b-chat-q4km", "tinyllama-1.1b-chat-v1.0.Q4_K_M.gguf",
            "https://huggingface.co/TheBloke/TinyLlama-1.1B-Chat-v1.0-GGUF/resolve/52e7645ba7c309695bec7ac98f4f005b139cf465/tinyllama-1.1b-chat-v1.0.Q4_K_M.gguf",
            "52e7645ba7c309695bec7ac98f4f005b139cf465",
            "9fecc3b3cd76bba89d504f29b616eedf7da85b96540e490ca5824d3f7d2776a0",
            1024ULL, 1536ULL, 668788096ULL,
            "TinyLlama-1.1B-Chat-v1.0", "llama",
            "Q4_K_M", "Apache-2.0"
        },
        {
            "qwen25-1.5b-instruct-q2k", "2", "Qwen2.5-1.5B-Instruct Q2_K (~0.7 GiB)",
            "conversation", "qwen25-1.5b-instruct-q2k", "qwen2.5-1.5b-instruct-q2_k.gguf",
            "https://huggingface.co/Qwen/Qwen2.5-1.5B-Instruct-GGUF/resolve/91cad51170dc346986eccefdc2dd33a9da36ead9/qwen2.5-1.5b-instruct-q2_k.gguf",
            "91cad51170dc346986eccefdc2dd33a9da36ead9",
            "5ede348e91ce1e7a330926ec5b202c27b864d065149dc463257fde1f98865b3a",
            832ULL, 1152ULL, 752880160ULL,
            "Qwen2.5-1.5B-Instruct", "qwen2",
            "Q2_K", "Apache-2.0"
        },
        {
            "qwen25-1.5b-instruct-q3km", "2", "Qwen2.5-1.5B-Instruct Q3_K_M (~0.9 GiB)",
            "conversation", "qwen25-1.5b-instruct-q3km", "qwen2.5-1.5b-instruct-q3_k_m.gguf",
            "https://huggingface.co/Qwen/Qwen2.5-1.5B-Instruct-GGUF/resolve/91cad51170dc346986eccefdc2dd33a9da36ead9/qwen2.5-1.5b-instruct-q3_k_m.gguf",
            "91cad51170dc346986eccefdc2dd33a9da36ead9",
            "58cb5c05ecef48e82961f1a2be6544145ea26136f69dddda4bbbd092f0e4b993",
            1024ULL, 1280ULL, 924455968ULL,
            "Qwen2.5-1.5B-Instruct", "qwen2",
            "Q3_K_M", "Apache-2.0"
        },
        {
            "qwen25-1.5b-instruct-q8", "3", "Qwen2.5-1.5B-Instruct Q8_0 (~1.8 GiB)",
            "conversation", "qwen25-1.5b-instruct-q8", "qwen2.5-1.5b-instruct-q8_0.gguf",
            "https://huggingface.co/Qwen/Qwen2.5-1.5B-Instruct-GGUF/resolve/91cad51170dc346986eccefdc2dd33a9da36ead9/qwen2.5-1.5b-instruct-q8_0.gguf",
            "91cad51170dc346986eccefdc2dd33a9da36ead9",
            "d7efb072e7724d25048a4fda0a3e10b04bdef5d06b1403a1c93bd9f1240a63c8",
            2048ULL, 2560ULL, 1894532128ULL,
            "Qwen2.5-1.5B-Instruct", "qwen2",
            "Q8_0", "Apache-2.0"
        },
        {
            "qwen25-3b-instruct-q2k", "2", "Qwen2.5-3B-Instruct Q2_K (~1.3 GiB)",
            "conversation", "qwen25-3b-instruct-q2k", "qwen2.5-3b-instruct-q2_k.gguf",
            "https://huggingface.co/Qwen/Qwen2.5-3B-Instruct-GGUF/resolve/7dabda4d13d513e3e842b20f0d435c732f172cbe/qwen2.5-3b-instruct-q2_k.gguf",
            "7dabda4d13d513e3e842b20f0d435c732f172cbe",
            "8ab4be7ea643fe2b207cf286ce032ca4f5b592f4cebb0db6aa6533c0b26f218d",
            1472ULL, 1920ULL, 1376856480ULL,
            "Qwen2.5-3B-Instruct", "qwen2",
            "Q2_K", "Apache-2.0"
        },
        {
            "qwen25-3b-instruct-q3km", "3", "Qwen2.5-3B-Instruct Q3_K_M (~1.6 GiB)",
            "conversation", "qwen25-3b-instruct-q3km", "qwen2.5-3b-instruct-q3_k_m.gguf",
            "https://huggingface.co/Qwen/Qwen2.5-3B-Instruct-GGUF/resolve/7dabda4d13d513e3e842b20f0d435c732f172cbe/qwen2.5-3b-instruct-q3_k_m.gguf",
            "7dabda4d13d513e3e842b20f0d435c732f172cbe",
            "ba8627a48c2bddefac2f995caf7887551304f26a72137fb94b74449121d0df4e",
            1856ULL, 2432ULL, 1724178848ULL,
            "Qwen2.5-3B-Instruct", "qwen2",
            "Q3_K_M", "Apache-2.0"
        },
        {
            "qwen25-3b-instruct-q8", "5", "Qwen2.5-3B-Instruct Q8_0 (~3.4 GiB)",
            "conversation", "qwen25-3b-instruct-q8", "qwen2.5-3b-instruct-q8_0.gguf",
            "https://huggingface.co/Qwen/Qwen2.5-3B-Instruct-GGUF/resolve/7dabda4d13d513e3e842b20f0d435c732f172cbe/qwen2.5-3b-instruct-q8_0.gguf",
            "7dabda4d13d513e3e842b20f0d435c732f172cbe",
            "6dcc22694c8654b045ec40bbe350212b88893fd9010e8474bae5b19a43578ba1",
            3840ULL, 4864ULL, 3616088480ULL,
            "Qwen2.5-3B-Instruct", "qwen2",
            "Q8_0", "Apache-2.0"
        },
        {
            "qwen25-7b-instruct-q2k", "4", "Qwen2.5-7B-Instruct Q2_K (~2.8 GiB)",
            "conversation", "qwen25-7b-instruct-q2k", "Qwen2.5-7B-Instruct-Q2_K.gguf",
            "https://huggingface.co/bartowski/Qwen2.5-7B-Instruct-GGUF/resolve/8911e8a47f92bac19d6f5c64a2e2095bd2f7d031/Qwen2.5-7B-Instruct-Q2_K.gguf",
            "8911e8a47f92bac19d6f5c64a2e2095bd2f7d031",
            "f04eb1416242987e53ceb567620a4bf7258f094705fc6c4086b24afaf85ed26c",
            3200ULL, 4096ULL, 3015940800ULL,
            "Qwen2.5-7B-Instruct", "qwen2",
            "Q2_K", "Apache-2.0"
        },
        {
            "qwen25-7b-instruct-q3km", "5", "Qwen2.5-7B-Instruct Q3_K_M (~3.5 GiB)",
            "conversation", "qwen25-7b-instruct-q3km", "Qwen2.5-7B-Instruct-Q3_K_M.gguf",
            "https://huggingface.co/bartowski/Qwen2.5-7B-Instruct-GGUF/resolve/8911e8a47f92bac19d6f5c64a2e2095bd2f7d031/Qwen2.5-7B-Instruct-Q3_K_M.gguf",
            "8911e8a47f92bac19d6f5c64a2e2095bd2f7d031",
            "6738a2d4f9b280c55b2a19a7ab27334a75d2cafc6ef82a11a075f4b5613eb736",
            4032ULL, 5120ULL, 3808391872ULL,
            "Qwen2.5-7B-Instruct", "qwen2",
            "Q3_K_M", "Apache-2.0"
        },
        {
            "qwen25-7b-instruct-q8", "16", "Qwen2.5-7B-Instruct Q8_0 (~7.5 GiB)",
            "conversation", "qwen25-7b-instruct-q8", "Qwen2.5-7B-Instruct-Q8_0.gguf",
            "https://huggingface.co/bartowski/Qwen2.5-7B-Instruct-GGUF/resolve/8911e8a47f92bac19d6f5c64a2e2095bd2f7d031/Qwen2.5-7B-Instruct-Q8_0.gguf",
            "8911e8a47f92bac19d6f5c64a2e2095bd2f7d031",
            "9c6a6e61664446321d9c0dd7ee28a0d03914277609e21bc0e1fce4abe780ce1b",
            8512ULL, 10752ULL, 8098525888ULL,
            "Qwen2.5-7B-Instruct", "qwen2",
            "Q8_0", "Apache-2.0"
        },
        {
            "llama32-3b-instruct-q3kl", "3", "Llama-3.2-3B-Instruct Q3_K_L (~1.7 GiB)",
            "conversation", "llama32-3b-instruct-q3kl", "Llama-3.2-3B-Instruct-Q3_K_L.gguf",
            "https://huggingface.co/bartowski/Llama-3.2-3B-Instruct-GGUF/resolve/5ab33fa94d1d04e903623ae72c95d1696f09f9e8/Llama-3.2-3B-Instruct-Q3_K_L.gguf",
            "5ab33fa94d1d04e903623ae72c95d1696f09f9e8",
            "38b11486260d779d1a81876f4c66ea2fd52e52f16d5a2044d992acf6532bfb07",
            1920ULL, 2432ULL, 1815347744ULL,
            "Llama-3.2-3B-Instruct", "llama",
            "Q3_K_L", "Llama-3.2"
        },
        {
            "llama32-3b-instruct-q8", "5", "Llama-3.2-3B-Instruct Q8_0 (~3.2 GiB)",
            "conversation", "llama32-3b-instruct-q8", "Llama-3.2-3B-Instruct-Q8_0.gguf",
            "https://huggingface.co/bartowski/Llama-3.2-3B-Instruct-GGUF/resolve/5ab33fa94d1d04e903623ae72c95d1696f09f9e8/Llama-3.2-3B-Instruct-Q8_0.gguf",
            "5ab33fa94d1d04e903623ae72c95d1696f09f9e8",
            "b5607b5090a8280063fff2d706bb3408ca6542341b06aab39c3eca0a28575921",
            3648ULL, 4608ULL, 3421899296ULL,
            "Llama-3.2-3B-Instruct", "llama",
            "Q8_0", "Llama-3.2"
        },
        {
            "llama31-8b-instruct-q2k", "5", "Meta-Llama-3.1-8B-Instruct Q2_K (~3.0 GiB)",
            "conversation", "llama31-8b-instruct-q2k", "Meta-Llama-3.1-8B-Instruct-Q2_K.gguf",
            "https://huggingface.co/bartowski/Meta-Llama-3.1-8B-Instruct-GGUF/resolve/bf5b95e96dac0462e2a09145ec66cae9a3f12067/Meta-Llama-3.1-8B-Instruct-Q2_K.gguf",
            "bf5b95e96dac0462e2a09145ec66cae9a3f12067",
            "3f7f9265554ac91f1fb31c1b63b9cada4362533dfaa3f6e8dfc33c48d36909ce",
            3392ULL, 4352ULL, 3179136416ULL,
            "Meta-Llama-3.1-8B-Instruct", "llama",
            "Q2_K", "Llama-3.1"
        },
        {
            "llama31-8b-instruct-q3km", "6", "Meta-Llama-3.1-8B-Instruct Q3_K_M (~3.7 GiB)",
            "conversation", "llama31-8b-instruct-q3km", "Meta-Llama-3.1-8B-Instruct-Q3_K_M.gguf",
            "https://huggingface.co/bartowski/Meta-Llama-3.1-8B-Instruct-GGUF/resolve/bf5b95e96dac0462e2a09145ec66cae9a3f12067/Meta-Llama-3.1-8B-Instruct-Q3_K_M.gguf",
            "bf5b95e96dac0462e2a09145ec66cae9a3f12067",
            "6be122b5c8f2a33974953e58e0ffd2be505661acc6f4caf733e4bca130e89fea",
            4224ULL, 5376ULL, 4018922912ULL,
            "Meta-Llama-3.1-8B-Instruct", "llama",
            "Q3_K_M", "Llama-3.1"
        },
        {
            "llama31-8b-instruct-q8", "16", "Meta-Llama-3.1-8B-Instruct Q8_0 (~8.0 GiB)",
            "conversation", "llama31-8b-instruct-q8", "Meta-Llama-3.1-8B-Instruct-Q8_0.gguf",
            "https://huggingface.co/bartowski/Meta-Llama-3.1-8B-Instruct-GGUF/resolve/bf5b95e96dac0462e2a09145ec66cae9a3f12067/Meta-Llama-3.1-8B-Instruct-Q8_0.gguf",
            "bf5b95e96dac0462e2a09145ec66cae9a3f12067",
            "9da71c45c90a821809821244d4971e5e5dfad7eb091f0b8ff0546392393b6283",
            8960ULL, 11264ULL, 8540775840ULL,
            "Meta-Llama-3.1-8B-Instruct", "llama",
            "Q8_0", "Llama-3.1"
        },
        {
            "llama32-1b-instruct-q3kl", "2", "Llama-3.2-1B-Instruct Q3_K_L (~0.7 GiB)",
            "conversation", "llama32-1b-instruct-q3kl", "Llama-3.2-1B-Instruct-Q3_K_L.gguf",
            "https://huggingface.co/bartowski/Llama-3.2-1B-Instruct-GGUF/resolve/067b946cf014b7c697f3654f621d577a3e3afd1c/Llama-3.2-1B-Instruct-Q3_K_L.gguf",
            "067b946cf014b7c697f3654f621d577a3e3afd1c",
            "4cc006968acfee52002f0d117d14cf2ed587e15c93a85c84fe8c053c5c47455d",
            832ULL, 1152ULL, 732524672ULL,
            "Llama-3.2-1B-Instruct", "llama",
            "Q3_K_L", "Llama-3.2"
        },
        {
            "llama32-1b-instruct-q8", "2", "Llama-3.2-1B-Instruct Q8_0 (~1.2 GiB)",
            "conversation", "llama32-1b-instruct-q8", "Llama-3.2-1B-Instruct-Q8_0.gguf",
            "https://huggingface.co/bartowski/Llama-3.2-1B-Instruct-GGUF/resolve/067b946cf014b7c697f3654f621d577a3e3afd1c/Llama-3.2-1B-Instruct-Q8_0.gguf",
            "067b946cf014b7c697f3654f621d577a3e3afd1c",
            "432f310a77f4650a88d0fd59ecdd7cebed8d684bafea53cbff0473542964f0c3",
            1408ULL, 1792ULL, 1321083008ULL,
            "Llama-3.2-1B-Instruct", "llama",
            "Q8_0", "Llama-3.2"
        },
        {
            "gemma2-2b-it-q3kl", "3", "Gemma-2-2B-it Q3_K_L (~1.4 GiB)",
            "conversation", "gemma2-2b-it-q3kl", "gemma-2-2b-it-Q3_K_L.gguf",
            "https://huggingface.co/bartowski/gemma-2-2b-it-GGUF/resolve/855f67caed130e1befc571b52bd181be2e858883/gemma-2-2b-it-Q3_K_L.gguf",
            "855f67caed130e1befc571b52bd181be2e858883",
            "d14b920ed8025a03e0764bdaeca6fe553dfb559569c79b5b83bb5b28fc364984",
            1664ULL, 2176ULL, 1550436192ULL,
            "Gemma-2-2B-it", "gemma",
            "Q3_K_L", "Gemma"
        },
        {
            "gemma2-2b-it-q8", "4", "Gemma-2-2B-it Q8_0 (~2.6 GiB)",
            "conversation", "gemma2-2b-it-q8", "gemma-2-2b-it-Q8_0.gguf",
            "https://huggingface.co/bartowski/gemma-2-2b-it-GGUF/resolve/855f67caed130e1befc571b52bd181be2e858883/gemma-2-2b-it-Q8_0.gguf",
            "855f67caed130e1befc571b52bd181be2e858883",
            "2d448a9aab894b8e8e18168cf3f490cb9f65632222f29f93514ac9ecc754debe",
            2944ULL, 3712ULL, 2784495456ULL,
            "Gemma-2-2B-it", "gemma",
            "Q8_0", "Gemma"
        },
        {
            "mistral-7b-instruct-v03-q2k", "4", "Mistral-7B-Instruct-v0.3 Q2_K (~2.5 GiB)",
            "conversation", "mistral-7b-instruct-v03-q2k", "Mistral-7B-Instruct-v0.3-Q2_K.gguf",
            "https://huggingface.co/bartowski/Mistral-7B-Instruct-v0.3-GGUF/resolve/61fd4167fff3ab01ee1cfe0da183fa27a944db48/Mistral-7B-Instruct-v0.3-Q2_K.gguf",
            "61fd4167fff3ab01ee1cfe0da183fa27a944db48",
            "6696020e424ccf3bc4ab6ac2bc639220f3d09ad6a225d5c3e16033a1ff9a4885",
            2880ULL, 3712ULL, 2722877664ULL,
            "Mistral-7B-Instruct-v0.3", "mistral",
            "Q2_K", "Apache-2.0"
        },
        {
            "mistral-7b-instruct-v03-q3km", "5", "Mistral-7B-Instruct-v0.3 Q3_K_M (~3.3 GiB)",
            "conversation", "mistral-7b-instruct-v03-q3km", "Mistral-7B-Instruct-v0.3-Q3_K_M.gguf",
            "https://huggingface.co/bartowski/Mistral-7B-Instruct-v0.3-GGUF/resolve/61fd4167fff3ab01ee1cfe0da183fa27a944db48/Mistral-7B-Instruct-v0.3-Q3_K_M.gguf",
            "61fd4167fff3ab01ee1cfe0da183fa27a944db48",
            "145b11df092f04bbecb9fd99e83d1f3d8a6d1e53805682bfba1fe314f306e1d5",
            3712ULL, 4736ULL, 3522941152ULL,
            "Mistral-7B-Instruct-v0.3", "mistral",
            "Q3_K_M", "Apache-2.0"
        },
        {
            "mistral-7b-instruct-v03-q8", "16", "Mistral-7B-Instruct-v0.3 Q8_0 (~7.2 GiB)",
            "conversation", "mistral-7b-instruct-v03-q8", "Mistral-7B-Instruct-v0.3-Q8_0.gguf",
            "https://huggingface.co/bartowski/Mistral-7B-Instruct-v0.3-GGUF/resolve/61fd4167fff3ab01ee1cfe0da183fa27a944db48/Mistral-7B-Instruct-v0.3-Q8_0.gguf",
            "61fd4167fff3ab01ee1cfe0da183fa27a944db48",
            "404857e776114baada71a08ebd3bba79d721ec7fca99705e7e7b892ae8bc583f",
            8128ULL, 10240ULL, 7702565088ULL,
            "Mistral-7B-Instruct-v0.3", "mistral",
            "Q8_0", "Apache-2.0"
        },
        {
            "granite31-2b-instruct-q2k", "2", "Granite-3.1-2B-Instruct Q2_K (~0.9 GiB)",
            "conversation", "granite31-2b-instruct-q2k", "granite-3.1-2b-instruct-Q2_K.gguf",
            "https://huggingface.co/bartowski/granite-3.1-2b-instruct-GGUF/resolve/e47b8b46c04cede00f9e19d5a846551b14b2efce/granite-3.1-2b-instruct-Q2_K.gguf",
            "e47b8b46c04cede00f9e19d5a846551b14b2efce",
            "1023b7a8866f63e30a5ae73831d11cca9bd2c54ee4ffca7fc0c7d7dc7277e5df",
            1088ULL, 1408ULL, 978245184ULL,
            "Granite-3.1-2B-Instruct", "granite",
            "Q2_K", "Apache-2.0"
        },
        {
            "granite31-2b-instruct-q3km", "2", "Granite-3.1-2B-Instruct Q3_K_M (~1.2 GiB)",
            "conversation", "granite31-2b-instruct-q3km", "granite-3.1-2b-instruct-Q3_K_M.gguf",
            "https://huggingface.co/bartowski/granite-3.1-2b-instruct-GGUF/resolve/e47b8b46c04cede00f9e19d5a846551b14b2efce/granite-3.1-2b-instruct-Q3_K_M.gguf",
            "e47b8b46c04cede00f9e19d5a846551b14b2efce",
            "5dbae7b1de10933b7e6858b39df7a5b0b1e78b0f560482deb7f1a654e1ff88e3",
            1344ULL, 1792ULL, 1251726912ULL,
            "Granite-3.1-2B-Instruct", "granite",
            "Q3_K_M", "Apache-2.0"
        },
        {
            "granite31-2b-instruct-q8", "4", "Granite-3.1-2B-Instruct Q8_0 (~2.5 GiB)",
            "conversation", "granite31-2b-instruct-q8", "granite-3.1-2b-instruct-Q8_0.gguf",
            "https://huggingface.co/bartowski/granite-3.1-2b-instruct-GGUF/resolve/e47b8b46c04cede00f9e19d5a846551b14b2efce/granite-3.1-2b-instruct-Q8_0.gguf",
            "e47b8b46c04cede00f9e19d5a846551b14b2efce",
            "883a1094022c40cb05f481fbe236b315cf2ca4f30ff84f8852aa3301e0edde72",
            2880ULL, 3712ULL, 2694110208ULL,
            "Granite-3.1-2B-Instruct", "granite",
            "Q8_0", "Apache-2.0"
        },
        {
            "granite31-8b-instruct-q2k", "4", "Granite-3.1-8B-Instruct Q2_K (~2.9 GiB)",
            "conversation", "granite31-8b-instruct-q2k", "granite-3.1-8b-instruct-Q2_K.gguf",
            "https://huggingface.co/bartowski/granite-3.1-8b-instruct-GGUF/resolve/7a0f633d54069de889707a76353bc28b70361d9f/granite-3.1-8b-instruct-Q2_K.gguf",
            "7a0f633d54069de889707a76353bc28b70361d9f",
            "95d08153fd5f2faf133034b49d0b8add9492dbb69905def43c3f1fee17d8ccb5",
            3264ULL, 4096ULL, 3103590880ULL,
            "Granite-3.1-8B-Instruct", "granite",
            "Q2_K", "Apache-2.0"
        },
        {
            "granite31-8b-instruct-q3km", "6", "Granite-3.1-8B-Instruct Q3_K_M (~3.7 GiB)",
            "conversation", "granite31-8b-instruct-q3km", "granite-3.1-8b-instruct-Q3_K_M.gguf",
            "https://huggingface.co/bartowski/granite-3.1-8b-instruct-GGUF/resolve/7a0f633d54069de889707a76353bc28b70361d9f/granite-3.1-8b-instruct-Q3_K_M.gguf",
            "7a0f633d54069de889707a76353bc28b70361d9f",
            "96008ca29e89c8728d4b043df8d93dc3f4052881bfee0d526e8139f4bec4c844",
            4224ULL, 5376ULL, 3996584416ULL,
            "Granite-3.1-8B-Instruct", "granite",
            "Q3_K_M", "Apache-2.0"
        },
        {
            "granite31-8b-instruct-q8", "16", "Granite-3.1-8B-Instruct Q8_0 (~8.1 GiB)",
            "conversation", "granite31-8b-instruct-q8", "granite-3.1-8b-instruct-Q8_0.gguf",
            "https://huggingface.co/bartowski/granite-3.1-8b-instruct-GGUF/resolve/7a0f633d54069de889707a76353bc28b70361d9f/granite-3.1-8b-instruct-Q8_0.gguf",
            "7a0f633d54069de889707a76353bc28b70361d9f",
            "70d56efa48c46d941790f17fa0486528e9aab8b82b4e453d2e09e932570a092a",
            9152ULL, 11520ULL, 8684246400ULL,
            "Granite-3.1-8B-Instruct", "granite",
            "Q8_0", "Apache-2.0"
        },
        {
            "olmo2-7b-instruct-q2k", "4", "OLMo-2-1124-7B-Instruct Q2_K (~2.7 GiB)",
            "conversation", "olmo2-7b-instruct-q2k", "OLMo-2-1124-7B-Instruct-Q2_K.gguf",
            "https://huggingface.co/bartowski/OLMo-2-1124-7B-Instruct-GGUF/resolve/01a56cca7da47f11851889af56ec36a9e75ceac8/OLMo-2-1124-7B-Instruct-Q2_K.gguf",
            "01a56cca7da47f11851889af56ec36a9e75ceac8",
            "46650c2d439036a139e3717937482292a2a8ace91cc58f4d780d60a1b07a2592",
            3008ULL, 3840ULL, 2858262080ULL,
            "OLMo-2-1124-7B-Instruct", "olmo2",
            "Q2_K", "Apache-2.0"
        },
        {
            "olmo2-7b-instruct-q3km", "5", "OLMo-2-1124-7B-Instruct Q3_K_M (~3.4 GiB)",
            "conversation", "olmo2-7b-instruct-q3km", "OLMo-2-1124-7B-Instruct-Q3_K_M.gguf",
            "https://huggingface.co/bartowski/OLMo-2-1124-7B-Instruct-GGUF/resolve/01a56cca7da47f11851889af56ec36a9e75ceac8/OLMo-2-1124-7B-Instruct-Q3_K_M.gguf",
            "01a56cca7da47f11851889af56ec36a9e75ceac8",
            "8be11292e60ffab039bb67a792129c8aaef3236c8ea5eca2cc503dd92f6e1c63",
            3840ULL, 4864ULL, 3651837504ULL,
            "OLMo-2-1124-7B-Instruct", "olmo2",
            "Q3_K_M", "Apache-2.0"
        },
        {
            "olmo2-7b-instruct-q8", "16", "OLMo-2-1124-7B-Instruct Q8_0 (~7.2 GiB)",
            "conversation", "olmo2-7b-instruct-q8", "OLMo-2-1124-7B-Instruct-Q8_0.gguf",
            "https://huggingface.co/bartowski/OLMo-2-1124-7B-Instruct-GGUF/resolve/01a56cca7da47f11851889af56ec36a9e75ceac8/OLMo-2-1124-7B-Instruct-Q8_0.gguf",
            "01a56cca7da47f11851889af56ec36a9e75ceac8",
            "8f0c8a9b40e89884adc725ac1dc1fe5dde98d6a4fff46ead4a0968faef9c8c69",
            8192ULL, 10240ULL, 7759896128ULL,
            "OLMo-2-1124-7B-Instruct", "olmo2",
            "Q8_0", "Apache-2.0"
        },
        {
            "smollm2-1.7b-instruct-q2k", "1", "SmolLM2-1.7B-Instruct Q2_K (~0.6 GiB)",
            "conversation", "smollm2-1.7b-instruct-q2k", "SmolLM2-1.7B-Instruct-Q2_K.gguf",
            "https://huggingface.co/unsloth/SmolLM2-1.7B-Instruct-GGUF/resolve/e933f1cdf73cc87cb67915bf5dd6ea81d36080ca/SmolLM2-1.7B-Instruct-Q2_K.gguf",
            "e933f1cdf73cc87cb67915bf5dd6ea81d36080ca",
            "0bef8188930ae76e83e462eadd5a0878b55f14bf076ea140c1b47bc368fff47f",
            768ULL, 1024ULL, 674583200ULL,
            "SmolLM2-1.7B-Instruct", "llama",
            "Q2_K", "Apache-2.0"
        },
        {
            "smollm2-1.7b-instruct-q3km", "2", "SmolLM2-1.7B-Instruct Q3_K_M (~0.8 GiB)",
            "conversation", "smollm2-1.7b-instruct-q3km", "SmolLM2-1.7B-Instruct-Q3_K_M.gguf",
            "https://huggingface.co/unsloth/SmolLM2-1.7B-Instruct-GGUF/resolve/e933f1cdf73cc87cb67915bf5dd6ea81d36080ca/SmolLM2-1.7B-Instruct-Q3_K_M.gguf",
            "e933f1cdf73cc87cb67915bf5dd6ea81d36080ca",
            "c11df3ad10008f80b80c8028b93c959448d1a7a2e5758ff4248eeab20941be51",
            960ULL, 1280ULL, 860181152ULL,
            "SmolLM2-1.7B-Instruct", "llama",
            "Q3_K_M", "Apache-2.0"
        },
        {
            "smollm2-1.7b-instruct-q8", "3", "SmolLM2-1.7B-Instruct Q8_0 (~1.7 GiB)",
            "conversation", "smollm2-1.7b-instruct-q8", "SmolLM2-1.7B-Instruct-Q8_0.gguf",
            "https://huggingface.co/unsloth/SmolLM2-1.7B-Instruct-GGUF/resolve/e933f1cdf73cc87cb67915bf5dd6ea81d36080ca/SmolLM2-1.7B-Instruct-Q8_0.gguf",
            "e933f1cdf73cc87cb67915bf5dd6ea81d36080ca",
            "0f3fb091804c48a561b42a4ca1be9ce2c353017187f74c48f52299cae790abe5",
            1920ULL, 2432ULL, 1820414624ULL,
            "SmolLM2-1.7B-Instruct", "llama",
            "Q8_0", "Apache-2.0"
        },
        {
            "tinyllama-1.1b-chat-q2k", "1", "TinyLlama-1.1B-Chat-v1.0 Q2_K (~0.4 GiB)",
            "conversation", "tinyllama-1.1b-chat-q2k", "tinyllama-1.1b-chat-v1.0.Q2_K.gguf",
            "https://huggingface.co/TheBloke/TinyLlama-1.1B-Chat-v1.0-GGUF/resolve/52e7645ba7c309695bec7ac98f4f005b139cf465/tinyllama-1.1b-chat-v1.0.Q2_K.gguf",
            "52e7645ba7c309695bec7ac98f4f005b139cf465",
            "030a469a63576d59f601ef5608846b7718eaa884dd820e9aa7493efec1788afa",
            512ULL, 640ULL, 483116416ULL,
            "TinyLlama-1.1B-Chat-v1.0", "llama",
            "Q2_K", "Apache-2.0"
        },
        {
            "tinyllama-1.1b-chat-q3km", "1", "TinyLlama-1.1B-Chat-v1.0 Q3_K_M (~0.5 GiB)",
            "conversation", "tinyllama-1.1b-chat-q3km", "tinyllama-1.1b-chat-v1.0.Q3_K_M.gguf",
            "https://huggingface.co/TheBloke/TinyLlama-1.1B-Chat-v1.0-GGUF/resolve/52e7645ba7c309695bec7ac98f4f005b139cf465/tinyllama-1.1b-chat-v1.0.Q3_K_M.gguf",
            "52e7645ba7c309695bec7ac98f4f005b139cf465",
            "ec461c4d2b60896152bca3978aa49cd70edad7298715e4222e12af1eefea2125",
            640ULL, 896ULL, 550819200ULL,
            "TinyLlama-1.1B-Chat-v1.0", "llama",
            "Q3_K_M", "Apache-2.0"
        },
        {
            "tinyllama-1.1b-chat-q8", "2", "TinyLlama-1.1B-Chat-v1.0 Q8_0 (~1.1 GiB)",
            "conversation", "tinyllama-1.1b-chat-q8", "tinyllama-1.1b-chat-v1.0.Q8_0.gguf",
            "https://huggingface.co/TheBloke/TinyLlama-1.1B-Chat-v1.0-GGUF/resolve/52e7645ba7c309695bec7ac98f4f005b139cf465/tinyllama-1.1b-chat-v1.0.Q8_0.gguf",
            "52e7645ba7c309695bec7ac98f4f005b139cf465",
            "a4c9bb1dbaa372f6381a035fa5c02ef087aaa1ff1f843a56a22328114f03fc59",
            1280ULL, 1664ULL, 1170781568ULL,
            "TinyLlama-1.1B-Chat-v1.0", "llama",
            "Q8_0", "Apache-2.0"
        },
    };
    return catalog;
}

}  // namespace masterai
