# MasterAI Model Category Taxonomy

The authoritative model root is `models/`.

Approved categories:

- `general-programming`
- `code-completion`
- `code-review`
- `debugging`
- `documentation`
- `embeddings-code-search`

Each installed model occupies `models/<category>/<model-id>/` and requires a `manifest.json`. Discovery alone never establishes trust. A model becomes ready only after path validation, manifest validation, file-size checks, digest verification when supplied, backend compatibility checks, and hardware suitability assessment.
