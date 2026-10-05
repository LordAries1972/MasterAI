# MasterAI Model Category Taxonomy

The authoritative model root is `models/`.

Approved categories:

- `general-programming`
- `code-completion`
- `code-review`
- `debugging`
- `documentation`
- `embeddings-code-search`
- `conversation`
- `music` -- Text-to-Music, Voice-to-Music, and Text-to-Voice models.
  Download/catalog support only: MasterAI's inference runtime is
  GGUF/llama.cpp-only, so a `music`-category model can be downloaded,
  verified, and reach Ready, but there is no runner that can load or
  generate from it yet.

Each installed model occupies `models/<category>/<model-id>/` and requires a `manifest.json`. Discovery alone never establishes trust. A model becomes ready only after path validation, manifest validation, file-size checks, digest verification when supplied, backend compatibility checks, and hardware suitability assessment.
