# How to Use Machine Learning and Models with MasterAI

## A classroom guide to understanding, preparing, teaching, evaluating, and safely using models

**Audience:** Administrators, developers, data owners, model trainers, evaluators, and technical users who want to understand machine learning before using MasterAI's model and Machine Learning facilities.

**Purpose:** This document teaches the mechanics of machine learning and models from first principles, then turns that knowledge into a clear, sequential workflow for setting up an existing model or developing a specialized model for use with MasterAI.

**Project basis:** This guide is derived from the architecture, security rules, model lifecycle, Machine Learning Abilities, and phased implementation status in [PLAN.md](PLAN.md). Where the plan describes a future capability, this guide labels it as planned rather than presenting it as currently executable.

**Last aligned with the plan:** 5 August 2026, through the scoped Phase 55 Machine Learning foundation.

---

## How to study this guide

Read Sections 1–10 in order the first time. They form the classroom theory: what learning is, what a model contains, how a transformer produces an answer, how training changes weights, how data and evaluation control the result, and which parts MasterAI can operate today. Then use Section 11 when installing an existing model and Section 12 when developing a specialized one.

The document uses four recurring labels:

- **Concept** explains what something means.
- **Mechanism** explains how it works internally.
- **Decision** explains when to choose it.
- **Gate** is a condition that must be satisfied before proceeding.

The major lessons are:

1. Machine learning foundations.
2. Model anatomy.
3. Transformer and inference mechanics.
4. Training mechanics.
5. Teaching-method selection.
6. Dataset design.
7. Evaluation and approval.
8. MasterAI's current capability boundary.
9. Existing-model setup.
10. Specialized-model development.
11. Security, troubleshooting, and final review.

Do not skip directly to training commands. A training tool can execute a bad plan much faster than a person can repair the resulting data leakage, licensing problem, or misleading evaluation.

---

## 1. Learning outcomes

By the end of this guide, you should be able to:

1. Explain what machine learning is and how it differs from conventional programming.
2. Describe what a model contains, what parameters and weights do, and how training changes them.
3. Explain tokenization, embeddings, transformer attention, inference, context windows, sampling, and hallucinations.
4. Distinguish prompting, project context, retrieval-augmented generation, fine-tuning, and training from scratch.
5. Select a suitable teaching method for a real requirement.
6. Design a controlled dataset with separate training, validation, and test data.
7. Set up a verified GGUF model for MasterAI's current `llama.cpp` inference path.
8. Organize a model-development project using the current Machine Learning administration foundation.
9. Build an evidence-based training and evaluation plan without leaking test data or accepting unsafe data.
10. Package, verify, benchmark, approve, deploy, monitor, and, when necessary, roll back a specialized model.

---

## 2. The essential idea: learning a function from examples

Conventional software is usually written as explicit rules:

```text
Input + rules written by a programmer -> output
```

For example, a conventional program can classify a file by checking its extension with an `if` statement. Machine learning is useful when the complete rule set would be too large, subtle, variable, or difficult to write by hand. Instead of specifying every rule, we provide examples and an objective. A training procedure searches for model parameters that make the model perform well on those examples.

```text
Examples + desired outcomes + learning algorithm -> learned model
Learned model + new input                         -> prediction or generated output
```

The compact mathematical description is:

```text
prediction = f(input; parameters)
          ŷ = f(x; θ)
```

Where:

- `x` is the input;
- `f` is the model architecture and its computation;
- `θ` represents all learned parameters, commonly called weights;
- `ŷ` is the model's prediction.

During training, the system compares the prediction with a desired answer, calculates an error called the **loss**, and adjusts the parameters to reduce future error.

```mermaid
flowchart LR
    A["Training example"] --> B["Model with current parameters"]
    B --> C["Prediction"]
    C --> D["Loss: compare prediction with target"]
    D --> E["Calculate gradients"]
    E --> F["Optimizer updates parameters"]
    F --> B
```

That loop may run thousands or millions of times. The model is not storing a conventional list of all answers. It is compressing useful statistical patterns into its parameters. This is why a model may generalize to a new example, but it is also why it may be confidently wrong.

### 2.1 What machine learning is not

Machine learning is not:

- guaranteed truth;
- human understanding in a machine;
- a database with perfect recall;
- automatically secure because it runs locally;
- automatically improved by adding more data;
- automatically production-ready when training finishes;
- a replacement for validation, authorization, audit, or ordinary software engineering.

A model is a probabilistic component. A dependable system surrounds it with verified data, explicit policy, deterministic controls, evaluation, monitoring, and human accountability. MasterAI's control plane is intended to provide those boundaries while keeping the inference runner isolated and replaceable.

### 2.2 The complete machine-learning system

People often use *the model* to mean the whole product. In practice, the model is only one component. A usable machine-learning system also needs data acquisition, preparation, policy, retrieval, runtime infrastructure, application logic, evaluation, and monitoring.

```mermaid
flowchart TD
    S["Approved source data"] --> D["Versioned dataset pipeline"]
    D --> T["Training or adaptation"]
    T --> A["Candidate model artifact"]
    A --> E["Evaluation and safety gates"]
    E -->|"approved"| R["Model registry and deployment package"]
    E -->|"rejected"| D
    R --> I["Inference runtime"]
    U["Authorized user request"] --> C["MasterAI control plane"]
    P["Policy and identity"] --> C
    K["Authorized project or retrieved knowledge"] --> C
    C --> I
    I --> O["Generated result"]
    O --> M["Monitoring and reviewed feedback"]
    M --> D
```

Each arrow represents a boundary that must preserve identity and provenance. If an output cannot be traced back through model, configuration, evidence, and authorization, the system is not ready for controlled use.

---

## 3. The main families of machine learning

Different problems require different kinds of learning. Begin with the task, not with a fashionable model name.

### 3.1 Supervised learning

Supervised learning uses labeled examples containing both an input and a desired result.

Examples:

- source code -> `secure` or `vulnerable`;
- support message -> issue category;
- equipment readings -> predicted failure risk;
- photograph -> object class;
- instruction -> approved response.

Common tasks include classification, regression, object detection, instruction tuning, and question answering.

### 3.2 Unsupervised learning

Unsupervised learning looks for structure without a supplied target label.

Examples:

- grouping similar documents;
- clustering customers by behavior;
- finding unusual activity;
- reducing a large feature space for visualization.

The discovered groups are patterns, not automatically meaningful business categories. A subject expert must interpret them.

### 3.3 Self-supervised learning

Self-supervised learning creates its learning signal from the data itself. Large language models are commonly pre-trained by predicting missing or next tokens. The text supplies both the input and the target without a person labeling every sentence.

This is how a foundation model acquires broad language and code patterns. It is expensive and does not by itself teach the model an organization's current private facts or approval rules.

### 3.4 Semi-supervised and active learning

Semi-supervised learning combines a smaller labeled dataset with a larger unlabeled dataset. Active learning asks a reviewer to label the examples that are expected to be most informative. Both can reduce labeling effort, but machine-suggested labels must remain distinguishable from reviewed labels.

### 3.5 Reinforcement and preference learning

These methods use rewards, rankings, or preferences rather than only one exact target answer. They are useful for teaching behavior such as response quality or tool-selection policy. Poorly designed rewards can teach shortcuts that score well while violating the true intention.

### 3.6 Transfer learning and fine-tuning

Transfer learning begins with an already trained model and adapts it to a new task. Fine-tuning is a common transfer-learning method. It usually requires much less data and compute than training a foundation model from scratch.

### 3.7 Classical models and neural networks

Not every job needs a language model.

| Problem | Often suitable starting point | Why |
|---|---|---|
| Numeric prediction | Linear model, decision tree, boosted trees | Efficient and measurable |
| Fixed category classification | Logistic regression, tree model, small neural network | Clear targets and metrics |
| Anomaly discovery | Statistical detector, isolation method, clustering | Designed for unusual patterns |
| Semantic text or code search | Embedding model plus vector index | Retrieves meaning-related items |
| Long-form language or code generation | Transformer language model | Produces variable-length sequences |
| Images, audio, or multiple media types | Specialized or multimodal neural model | Architecture matches input structure |

The best model is the smallest, simplest model that satisfies the measured requirement within the security, latency, memory, and maintenance limits.

---

## 4. What a model actually contains

A model is the combination of several related artifacts and decisions. The weights alone are not enough to reproduce its behavior.

### 4.1 Architecture

The architecture defines the computation graph: which layers exist, how information flows, and what operations are performed. Examples include linear models, decision trees, convolutional networks, and transformers.

Architecture is comparable to the design of a machine. Parameters are the adjustable settings inside that design.

### 4.2 Parameters and weights

Parameters are learned numeric values. A neural network can contain millions or billions of them. During inference, the model repeatedly multiplies and combines inputs with these values.

Parameter count affects capacity and usually affects memory and compute, but a larger model is not automatically better for a particular task. Data quality, architecture, training procedure, quantization, prompt design, and evaluation all matter.

### 4.3 Hyperparameters

Hyperparameters control training rather than being directly learned. Examples include:

- learning rate;
- batch size;
- epoch count;
- optimizer;
- weight decay;
- context or sequence length;
- dropout;
- gradient accumulation and clipping;
- adapter rank for low-rank adaptation;
- random seed;
- checkpoint and validation frequency.

Hyperparameters must be recorded with every experiment. Without them, a result is not reproducible.

### 4.4 Tokenizer and vocabulary

A language model does not directly read words. A tokenizer converts text into integer token identifiers. A token may represent a word, part of a word, punctuation, whitespace, or code fragment.

```mermaid
flowchart LR
    A["Raw text: int total = 42;"] --> B["Tokenizer"]
    B --> C["Token pieces"]
    C --> D["Token identifiers"]
    D --> E["Model"]
```

The tokenizer is part of the model identity. Using the wrong tokenizer changes the input symbols and can make valid weights unusable.

### 4.5 Embeddings

An embedding maps a discrete item, such as a token or document chunk, to a numeric vector. During language-model inference, token embeddings are internal model inputs. In semantic search, document and query embeddings are compared to locate meaning-related content.

Vectors that are near one another under a chosen distance metric are treated as similar. They are not proof that two documents are factually equivalent.

```text
"C++ memory ownership" -> [ 0.12, -0.41, 0.08, ... ]
"RAII resource safety" -> [ 0.10, -0.39, 0.11, ... ]
```

An embedding model, its exact version, vector dimensions, distance metric, chunking rules, and index must be versioned together.

### 4.6 Checkpoints, adapters, and final artifacts

- A **checkpoint** is a saved training state. It may include weights, optimizer state, scheduler state, and progress.
- An **adapter** contains a smaller set of learned changes applied to a base model.
- A **merged model** combines an adapter with the base weights.
- A **quantized model** stores weights at reduced precision to lower memory and often improve inference speed.
- A **GGUF** is an inference-oriented model file format supported by MasterAI's current `llama.cpp` adapter.

Quantization changes the representation used for inference. It is not the same as teaching the model. It may reduce quality, so the quantized result must be evaluated separately from the source checkpoint.

### 4.7 A complete model identity

For controlled use, identify a model with all of the following:

```text
Architecture + exact weights + tokenizer + template + configuration
+ source revision + license + hashes + runtime + evaluation evidence
```

A filename is not identity. MasterAI therefore requires model metadata and an exact SHA-256 digest before an inference model can become Ready.

### 4.8 From one artificial neuron to a network

A simplified artificial neuron receives input values, multiplies each by a learned weight, adds a learned bias, and applies an activation function:

```text
z = (w₁x₁ + w₂x₂ + ... + wₙxₙ) + b
output = activation(z)
```

The weights control how strongly each input influences the result. The bias shifts the decision. The activation function allows a network to represent nonlinear relationships rather than collapsing into one large linear calculation.

```mermaid
flowchart LR
    X1["Input x1"] -->|"weight w1"| SUM["Weighted sum plus bias"]
    X2["Input x2"] -->|"weight w2"| SUM
    X3["Input x3"] -->|"weight w3"| SUM
    B["Bias b"] --> SUM
    SUM --> ACT["Activation function"]
    ACT --> Y["Output"]
```

A neural network connects many such transformations into layers:

- the **input layer** receives features or embeddings;
- **hidden layers** form increasingly useful intermediate representations;
- the **output layer** produces task scores, numeric predictions, or next-token logits.

The term *deep learning* generally means that the network has many learned transformation layers. Depth gives the network greater representational capacity, but also increases its data, compute, debugging, and evaluation requirements.

### 4.9 Features and representations

A feature is information presented to a model. In a classical tabular model, a person may explicitly supply features such as file size, error count, or transaction age. In a neural network, lower layers often learn useful representations from rawer inputs.

Feature design still matters. Units, missing values, normalization, category encoding, image dimensions, audio sample rates, tokenization, and document chunking all change what the model can learn. A model cannot recover information that the data pipeline consistently discarded.

### 4.10 Architecture, weights, runtime, and application are separate

Keep these boundaries clear:

| Layer | Example | Can it change model knowledge or behavior? |
|---|---|---|
| Architecture | Transformer layer arrangement | Defines capacity and computation |
| Weights or adapter | Learned numeric parameters | Yes; this is what training changes |
| Tokenizer and template | Text-to-token rules and role formatting | Changes how the same text is presented |
| Runtime | `llama.cpp`, context size, batching, quantization support | Changes execution and may affect outputs/performance |
| Prompt and retrieved context | Instructions and current evidence | Changes this request, not stored weights |
| MasterAI control plane | Identity, authorization, storage, policy, routing, audit | Governs access and use; it is not the model |

This separation is central to MasterAI. The native C++17 control plane must remain authoritative even though an isolated external runner performs model inference.

---

## 5. How a transformer language model works

MasterAI's current local inference path is aimed at GGUF language and programming models behind an isolated `llama.cpp` server. Understanding a transformer makes the visible behavior less mysterious.

### 5.1 From prompt to output

```mermaid
flowchart TD
    A["System instructions"] --> P["Prompt assembly"]
    B["Conversation history"] --> P
    C["Authorized project context"] --> P
    D["Current user request"] --> P
    P --> T["Tokenizer"]
    T --> E["Token embeddings and positions"]
    E --> L["Repeated transformer layers"]
    L --> H["Output logits for next token"]
    H --> S["Sampling or deterministic selection"]
    S --> O["Next token"]
    O -->|"append and repeat"| L
    O --> X["Stream token to the user"]
```

The model generates one token at a time. Each new token becomes part of the next step's input until an end token, a configured output limit, cancellation, or an error stops generation.

### 5.2 Attention

Attention allows each position to combine information from relevant earlier positions. At a simplified level, each token creates:

- a **query** describing what information it is seeking;
- a **key** describing what information it offers;
- a **value** containing the information to combine.

The model compares queries with keys, converts the scores into attention weights, and forms a weighted mixture of values.

```text
Attention(Q, K, V) = softmax(QKᵀ / √d) V
```

```mermaid
flowchart LR
    A["Token representations"] --> Q["Queries"]
    A --> K["Keys"]
    A --> V["Values"]
    Q --> M["Similarity scores"]
    K --> M
    M --> W["Normalized attention weights"]
    W --> C["Weighted combination"]
    V --> C
    C --> N["Next layer representation"]
```

This mechanism helps a model connect names with later references, opening braces with closing logic, or a question with relevant context. It does not guarantee that the attended information is true or that the model will follow it correctly.

### 5.3 Transformer layers

A transformer stacks attention, feed-forward transformations, normalization, and residual connections. Early layers often capture local or syntactic patterns; later layers can represent more abstract relationships. This is an observed tendency, not a simple one-layer-per-concept map.

### 5.4 Logits, probabilities, and sampling

The final layer produces one score, called a logit, for every possible next token. Those scores are converted into probabilities. Generation settings control how the next token is selected.

- Lower temperature makes high-probability choices more dominant.
- Higher temperature increases variety and risk.
- A fixed seed can improve repeatability, but hardware and runtime differences may still matter.
- Greedy selection chooses the highest-scoring token but does not make the answer factual.

### 5.5 Context windows

The context window is the bounded token space visible during one inference sequence. It may contain system instructions, chat history, retrieved material, project context, tool results, and the current request.

When the context is too large, the system must omit, summarize, rank, or reject content. Increasing context length consumes more memory, especially for the key-value cache. It is not free storage and is not permanent learning.

### 5.6 KV-cache reuse

During attention, the runner calculates key and value representations for earlier tokens. Reusing those values can avoid recalculating an unchanged prompt prefix. MasterAI only permits session reuse when model, backend, template, settings, project index generation, and literal prompt-prefix conditions match. A changed model or context invalidates reuse.

KV-cache reuse improves performance. It does not modify model weights or teach the model.

### 5.7 Why hallucinations occur

A generative model predicts plausible continuations. It does not consult a built-in truth oracle. Hallucinations are more likely when:

- the requested fact was absent, rare, conflicting, or outdated in training;
- supplied context is missing or ambiguous;
- the prompt rewards confident completion;
- sampling is too permissive;
- retrieved evidence is irrelevant;
- the model is too small or unsuitable for the task;
- the task demands exact calculation, citation, or tool state not available to the model.

Controls include retrieval with provenance, constrained outputs, deterministic code, tools, explicit uncertainty, evaluation, and human review.

---

## 6. How training changes a model

### 6.1 The training loop

For a supervised language example, the model receives input tokens and tries to predict target tokens. The loss measures how poorly it assigned probability to the desired output.

```mermaid
sequenceDiagram
    participant D as Dataset batch
    participant M as Model
    participant L as Loss function
    participant B as Backpropagation
    participant O as Optimizer
    D->>M: Input and expected target
    M->>L: Predicted token probabilities
    D->>L: Expected target tokens
    L->>B: Scalar loss
    B->>O: Gradient for each trainable parameter
    O->>M: Small parameter update
```

The steps are:

1. Select a batch of training examples.
2. Run a forward pass to calculate predictions.
3. Calculate loss against the targets.
4. Backpropagate the loss to calculate gradients.
5. Clip or otherwise control unsafe gradients if configured.
6. Let the optimizer update trainable parameters.
7. Record metrics.
8. Periodically evaluate on validation data.
9. Save recoverable checkpoints.
10. Stop when the objective is met, progress stalls, or a safety/resource boundary is reached.

### 6.2 Epochs, steps, and batches

- A **sample** is one training record.
- A **batch** is a group processed before an optimizer update.
- A **step** is usually one optimizer update.
- An **epoch** is one pass through the selected training data.

More epochs can improve learning until the model begins memorizing noise or overspecializing. Validation behavior matters more than a chosen epoch count by itself.

### 6.3 Learning rate

The learning rate controls update size. Too high may destabilize or erase useful prior behavior; too low may learn very slowly or stall. A schedule can warm up, reduce, or otherwise change the rate during training.

### 6.4 Backpropagation and gradients

A gradient estimates how a small parameter change would affect loss. Backpropagation applies the chain rule through the computation graph to calculate these gradients efficiently. The optimizer uses them to update weights.

This process is numerical optimization. It does not insert a human-readable rule into a known location. That is why dataset design and evaluation are essential.

### 6.5 Overfitting and underfitting

**Underfitting** occurs when the model has not learned the required pattern. Training and validation results are both poor.

**Overfitting** occurs when the model fits training examples too closely and performs poorly on unseen examples. Training results improve while validation or test results stagnate or degrade.

Controls include better data, regularization, early stopping, fewer epochs, smaller capacity, data augmentation, and a genuinely independent test set.

### 6.6 Catastrophic forgetting

Aggressive fine-tuning can damage capabilities learned by the base model. Always run both target-subject tests and broad regression tests. Parameter-efficient methods can reduce, but do not eliminate, this risk.

### 6.7 Fine-tuning methods

| Method | What changes | Typical benefit | Main caution |
|---|---|---|---|
| Full fine-tuning | Most or all weights | Maximum adaptation capacity | Highest compute and forgetting risk |
| Adapter training | Added small trainable modules | Modular specialization | Runtime must load the correct adapter |
| LoRA | Low-rank weight updates | Lower memory and storage | Rank and target layers require testing |
| Quantized LoRA | LoRA with a quantized base during training | Makes adaptation possible on smaller hardware | Numerical and conversion quality must be checked |
| Preference optimization | Behavior from preferred/rejected outputs | Improves style or choice behavior | Preferences may encode bias or shortcuts |
| Distillation | Smaller model learns from a larger teacher | Lower inference cost | Can inherit teacher errors |

### 6.8 Training from scratch

Training a foundation language model from scratch requires enormous datasets, compute, engineering, filtering, evaluation, and legal review. It is an explicit non-goal for MasterAI's initial release. For most MasterAI use cases, begin with an approved base model and use retrieval, prompting, or parameter-efficient fine-tuning.

### 6.9 A small numerical training example

Consider a deliberately simple model that predicts `y = wx` and begins with `w = 0.5`. For the example `x = 2` and target `y = 4`:

1. Prediction: `ŷ = 0.5 × 2 = 1`.
2. Error: `ŷ - y = 1 - 4 = -3`.
3. Squared loss: `(-3)² = 9`.
4. The gradient says how the loss changes when `w` changes.
5. The optimizer moves `w` a small distance in the loss-reducing direction.
6. On the next pass, the prediction should be closer to `4` if the learning rate is suitable.

A language model applies the same broad principle to a vastly larger differentiable computation. Instead of one weight and one numeric target, it has many parameters and target token probabilities. The optimizer still follows gradients; it does not reason about the meaning of the lesson as a human teacher would.

### 6.10 What training metrics are telling you

| Signal | What it can indicate | What it cannot prove alone |
|---|---|---|
| Training loss | Fit to batches the optimizer sees | Generalization or factual accuracy |
| Validation loss | Fit to held-out development data | Final unbiased test quality |
| Learning rate | Current update scale | Whether the chosen objective is correct |
| Gradient norm | Update stability or possible explosion | Subject competence |
| Throughput | Training speed | Output quality |
| GPU/CPU memory | Resource demand | Efficient production inference |
| Checkpoint saved | Recoverable parameter state | Approved model quality |

When training and validation loss both improve, the candidate may be learning a useful pattern. When training improves but validation worsens, suspect overfitting. When neither improves, inspect data, targets, architecture, optimizer, and learning rate before merely extending the run.

---

## 7. Four ways to “teach” a model

The word *teach* is used loosely. Choose the mechanism that matches the required kind of change.

### 7.1 Prompting: temporary instructions

Prompting supplies instructions or examples inside the current context. It changes neither model weights nor permanent knowledge.

Use prompting when:

- behavior can be described clearly;
- the task is short-lived;
- rapid iteration matters;
- the instruction fits comfortably in context.

### 7.2 Project context: current working evidence

MasterAI can attach authorized project content and retrieved indexed chunks to a request. This lets the model reason over current source without retraining. The current project retrieval foundation is lexical and disk-backed; the plan's semantic embedding and richer RAG work should not be assumed complete merely because vector-store or RAG metadata exists.

Use project context when:

- answers must reflect the current repository;
- source changes frequently;
- model responses should be grounded in files;
- the knowledge must stay separately controllable.

### 7.3 Retrieval-augmented generation: managed external knowledge

RAG retrieves relevant approved information at request time and includes it in the prompt. The model's weights remain unchanged.

```mermaid
flowchart LR
    Q["User question"] --> QE["Query processing"]
    QE --> R["Authorized retrieval"]
    DS[("Versioned documents or vector store")] --> R
    R --> K["Ranked evidence with provenance"]
    K --> P["Prompt assembly"]
    Q --> P
    P --> M["Model inference"]
    M --> A["Grounded answer and citations"]
```

Use RAG when knowledge changes, citations are required, documents are private, facts must be deletable, or the subject is too large for a fine-tuning set.

### 7.4 Fine-tuning: persistent behavior adaptation

Fine-tuning changes weights or adapters. It is suited to repeated behavior, terminology, style, output formats, specialized task patterns, or tool-use behavior.

Fine-tuning is usually a poor way to maintain a frequently changing factual database. A model may blend or distort facts, cannot reliably cite their source, and is harder to update or delete than a document store.

### 7.5 The decision tree

```mermaid
flowchart TD
    A["What must improve?"] --> B{"Current facts or private documents?"}
    B -->|"Yes"| C["Use project context or RAG"]
    B -->|"No"| D{"Stable repeated behavior, style, format, or terminology?"}
    D -->|"Yes"| E["Try prompt and examples first"]
    E --> F{"Prompt meets evaluation target?"}
    F -->|"Yes"| G["Keep prompting; avoid training cost"]
    F -->|"No"| H["Fine-tune or train an adapter"]
    D -->|"No"| I{"Simple measurable prediction?"}
    I -->|"Yes"| J["Use a classical or small specialized model"]
    I -->|"No"| K["Reassess task and evidence"]
    C --> L{"Also needs specialized behavior?"}
    L -->|"Yes"| M["Combine RAG with fine-tuning"]
    L -->|"No"| N["Evaluate RAG alone"]
```

### 7.6 Comparison table

| Method | Changes weights | Knowledge freshness | Citations | Cost | Current MasterAI position |
|---|---:|---:|---:|---:|---|
| Prompting | No | Per request | Only if supplied | Low | Usable through chat |
| Project context | No | Re-indexed project state | Evidence disclosure is possible | Low to medium | Lexical foundation exists |
| Full semantic RAG | No | Update/re-index documents | Strong fit | Medium | Planned beyond current metadata foundation |
| Adapter or LoRA fine-tuning | Yes, partially | Static until retrained | Weak by itself | Medium | Job records exist; executor is not yet implemented |
| Full fine-tuning | Yes | Static until retrained | Weak by itself | High | Planned executor |
| Foundation training | Yes, from initialization | Static until retrained | Weak by itself | Extreme | Initial-release non-goal |

---

## 8. Data is the curriculum

A model learns from the examples and objectives it receives, not from the trainer's unrecorded intentions. Dataset engineering is therefore part of model engineering.

### 8.1 Define the target before collecting data

Write a measurable capability statement:

> Given a self-contained C++17 function and its surrounding declarations, the model identifies ownership defects, explains the failure mechanism, and proposes a compilable correction without introducing non-C++17 dependencies.

Then define:

- intended users and use cases;
- prohibited uses;
- input and output contract;
- subject scope and exclusions;
- quality and safety thresholds;
- latency and resource ceilings;
- deployment environment;
- required evidence or citations;
- rollback conditions.

Without this contract, “make the model better” cannot be evaluated.

### 8.2 Source approval and provenance

For every source, record:

- owner and origin;
- license and permitted use;
- acquisition date and immutable revision where possible;
- security classification;
- personal information, credentials, and secrets status;
- integrity checksum;
- allowed projects and users;
- retention and deletion requirements.

Do not use material merely because it is accessible. Access does not establish permission to train on, reproduce, or publish it.

### 8.3 Cleaning and normalization

A controlled preparation pipeline should:

1. Verify files and types.
2. Scan for malicious content.
3. Decode text deterministically.
4. Remove irrelevant boilerplate.
5. Normalize only what the task permits.
6. Detect duplicates and near-duplicates.
7. find secrets, credentials, and personal information.
8. Reject or redact unsafe records.
9. Segment content without destroying meaning.
10. Validate schema and labels.
11. assign quality scores.
12. Require human review where policy demands it.
13. Publish an immutable dataset version and checksum.

### 8.4 Training, validation, and test separation

```mermaid
flowchart TD
    A["Approved deduplicated source records"] --> B{"Group-aware split"}
    B --> C["Training set: fit parameters"]
    B --> D["Validation set: choose settings and stopping point"]
    B --> E["Test set: one final unbiased measurement"]
    C -. "no duplicate families" .-> D
    D -. "no tuning on test" .-> E
```

- **Training data** updates the model.
- **Validation data** selects hyperparameters and checkpoints.
- **Test data** estimates final performance and must not influence training decisions.

Split by meaningful groups, not just random rows. For example, keep all functions from the same repository, all variants of one question, or all messages from one conversation in the same partition. Otherwise near-duplicates can leak answers across sets.

### 8.5 Labels and instruction examples

A useful instruction record may contain:

- system instruction;
- user instruction;
- relevant context;
- expected response;
- rejected response where preference training is intended;
- permitted tool calls and expected results;
- required output format;
- subject and difficulty;
- safety classification;
- reviewer identity and status.

Examples should teach both desired behavior and boundaries. Include ordinary cases, edge cases, malformed inputs, ambiguous cases, safe refusals, and cases where the correct response is to request missing evidence.

### 8.6 Synthetic data

Synthetic examples can broaden coverage, but they can also amplify a generator's errors. Record the generator model, version, prompt, settings, source linkage, confidence, and review status. Never silently mix synthetic records with verified real records.

### 8.7 Dataset quality checklist

Before approval, confirm:

- scope matches the capability contract;
- records are valid and readable;
- labels are consistent;
- sources and licenses are documented;
- secrets and personal data are handled;
- duplicates and contamination are measured;
- classes and difficulty are appropriately balanced;
- train, validation, and test partitions are isolated;
- adversarial and negative cases exist;
- the version and checksum are fixed;
- reviewers have approved the result.

---

## 9. Evaluation: proving that learning occurred

Training loss is an optimization signal, not deployment approval.

### 9.1 Build the evaluation before training

Create a baseline evaluation set before changing the model. Run it against the base model and record the result. This establishes whether training actually improves the target capability.

### 9.2 Select task-appropriate metrics

Examples include:

- accuracy, precision, recall, and F1 for classification;
- mean absolute or squared error for regression;
- retrieval recall, ranking quality, groundedness, and citation correctness for RAG;
- compilation and unit-test success for code generation;
- structured-output conformance for machine-consumed responses;
- hallucination and unsupported-claim rate for factual tasks;
- safety-policy compliance and adversarial resistance;
- latency, throughput, memory, GPU use, model-load time, and failure rate.

One aggregate score can hide serious failures. Report results by topic, difficulty, user group, and risk category.

### 9.3 Compare against a baseline

Use the same:

- dataset version;
- evaluation harness;
- prompt or chat template;
- runtime and model settings;
- hardware identity;
- random-seed policy;
- output limits;
- scoring rules.

Compare target improvement, broad regressions, safety behavior, and resource cost. A specialized model that gains two points on its subject but loses critical tool-use safety should fail approval.

### 9.4 Human evaluation

Human review is essential when correctness is contextual or multiple responses are valid. Blind pairwise comparison reduces brand and version bias. Reviewers need a rubric and should record reasons, not only preferences.

### 9.5 Approval gates

```mermaid
stateDiagram-v2
    [*] --> Unverified
    Unverified --> Verified: identity, license, hash, format
    Verified --> Training: approved job begins
    Training --> Evaluation: training completed
    Evaluation --> Rejected: quality, safety, or resource failure
    Evaluation --> Approved: all thresholds and reviews pass
    Approved --> Staging: controlled deployment
    Staging --> Production: staging evidence and approval
    Production --> Deprecated: replacement or drift
    Production --> Quarantined: integrity or safety incident
    Deprecated --> Archived
    Rejected --> Training: revised data or configuration
```

Training completion must never transition a model directly to production.

---

## 10. MasterAI's current capability boundary

This distinction is essential when following the practical lessons below.

### 10.1 What is usable now

MasterAI currently provides a real local inference control plane for verified GGUF models through an optional, version-pinned, process-isolated `llama.cpp` server. It includes model discovery and verification, hardware suitability checks, chat, project context, benchmarking, resource admission, runner supervision, cancellation, and administrative controls.

The Machine Learning administration foundation currently records and manages scoped lifecycle metadata for:

- the Machine Learning dashboard;
- Machine Learning projects;
- model registry entries;
- datasets;
- subject knowledge packages;
- labeling tasks;
- data-preparation jobs;
- training jobs;
- evaluation runs;
- experiments;
- fine-tuning jobs;
- model-builder configurations;
- instruction examples;
- synthetic-data records;
- vector-store registrations;
- retrieval-augmented-generation configuration registrations;
- subject exams;
- hyperparameter searches;
- model-optimization runs;
- training checkpoints;
- deployments;
- model comparisons (whose **Compare now** action is a real executor — see below).

These are valuable control-plane records. They establish identity, ownership, intent, relationships, status, review, and approval boundaries.

Since Phase 56, the module also contains a **real execution engine** for tabular machine learning, extended by Phase 57. This is genuine computation, not record-keeping:

- **Dataset content ingestion** — uploading CSV text to a registered dataset parses and validates it completely (header row, numeric feature enforcement, quoted fields, an 8 MiB cap) before anything is stored, and returns a real profile: row count, feature columns, the target column, and whether the task is classification or regression.
- **A real training executor** — running a training job performs actual full-batch gradient descent on standardized features: linear regression when the target column is numeric, logistic or softmax classification when it is categorical. The job's lifecycle transitions (`queued → preparing → running → awaiting_evaluation`) happen for real, the per-epoch loss curve comes from real optimization steps, and checkpoint records carry genuinely measured losses.
- **Persisted learned weights** — every completed run stores the trained weights, standardization statistics, and schema as a reloadable artifact tied to a Model Registry entry, so a model trained today still predicts correctly after a server restart.
- **A real evaluation harness** — running an evaluation scores a trained model against any schema-matching dataset and stores genuine metrics: accuracy, macro precision/recall/F1, and a confusion matrix for classification; MSE, MAE, and R² for regression.
- **Live prediction** — the Model Registry serves real predictions (the winning class with per-class probabilities, or the predicted numeric value) computed from the persisted weights.
- **A real comparison harness (Phase 57)** — a Model Comparison names a baseline model, a candidate model, and one shared benchmark dataset; **Compare now** evaluates both trained artifacts against that dataset's real content and stores a measured verdict: both full metric sets, the primary-metric delta (macro F1 for classification, MSE for regression), and the winner. An exact tie is reported as a tie, and comparing a classification model against a regression model is rejected.

Section 10.7 walks through this end to end. The engine's own honest boundary: it trains **tabular** models. It does not fine-tune language models — that remains planned, and the boundary list below still applies to everything it names.

### 10.2 What must not be assumed yet

At the plan's current Phase 57 boundary, the tabular engine described in 10.1 is real, but the following must still not be represented as an operating end-to-end platform:

- training or fine-tuning of **language models** (the real executor covers tabular classification and regression only);
- external training-framework integration (PyTorch, llama.cpp finetune, ...);
- immutable dataset version publication and transformation execution (Data Preparation jobs still record intent only);
- real label-record editing and consensus review;
- checkpoint **weight** capture and resume (Phase 56 checkpoints carry genuinely measured losses, but not restorable weight snapshots — the final weights live in the model artifact);
- adapter (LoRA) execution;
- full experiment metric and artifact capture (Experiment records remain metadata);
- exam administration or computed exam results;
- automated hyperparameter search trials or best-result selection;
- executed quantization, pruning, distillation, or other optimization operations;
- embedding generation or a populated vector index;
- a complete executable RAG pipeline;
- deployment automation, canary rollout, health monitoring, or automatic rollback;
- continual learning from user conversations.

Creating a Fine-Tuning Job record still records administrative intent and lifecycle state; it does not adapt a language model. Creating a Vector Store record does not generate embeddings or index documents. Likewise, a Subject Exam record does not administer questions, a Hyperparameter Search record does not run trials, a Model Optimization record does not quantize anything, and an approved Deployment record does not route a single request — each is a governed statement of intent and approval that a future executor will attach real evidence to. The deliberate exceptions since Phases 56-57: a Training Job whose dataset has uploaded CSV content, run through **Train now**, genuinely trains; an Evaluation Run executed through **Evaluate now** genuinely scores; and a Model Comparison executed through **Compare now** genuinely measures which of two trained models is better on a shared benchmark.

### 10.3 Two model catalogs with different purposes

Do not confuse these concepts:

1. The **Machine Learning Model Registry** records the development and approval lifecycle of a model asset.
2. The **inference model tree** under `models/<category>/<model-id>/` contains a deployable GGUF plus a strict `manifest.json` used by the current runtime.

A future integrated workflow should promote an approved ML result into the inference model tree. Today, an administrator must deliberately package and verify the resulting GGUF for inference.

### 10.4 When to use MasterAI today

Use MasterAI now to:

- define and track the controlled ML project;
- register datasets, subjects, examples, jobs, experiments, and approvals as the supported fields permit;
- upload tabular CSV data and train, evaluate, compare, and serve real classification/regression models end to end (Section 10.7);
- keep ordinary users away from ML administration;
- run and compare verified GGUF inference models;
- test prompts and authorized project context;
- benchmark a model on the same host;
- serve an approved specialized GGUF after it has been trained and converted by a controlled external workflow.

Do not use a manually changed status as proof that anything ran. Trust the executor's own evidence instead: a Phase 56 training run leaves a real loss curve, held-out metrics, measured-loss checkpoints, and a weight artifact behind — a hand-edited status leaves nothing.

### 10.5 Current Machine Learning classroom map

The current administrator pages are easiest to understand as a chain of controlled records. The table explains what each page is for and the boundary a learner must remember.

| Interface | Browser path | Record's present purpose | Important present boundary |
|---|---|---|---|
| Dashboard | `/app/ml` | Shows real counts and the interface roadmap | A zero count is real; a planned tag is not a hidden implementation |
| Projects | `/app/ml/projects` | Organizes one ML objective and lifecycle | Does not allocate compute or storage |
| Model Registry | `/app/ml/models` | Tracks model identity, provenance summary, and state; serves live predictions from trained tabular artifacts | Is not the deployable GGUF inventory |
| Dataset Manager | `/app/ml/datasets` | Tracks dataset identity, source, purpose, and approval; holds real validated CSV content for tabular training | Does not publish immutable versions or ingest non-tabular corpora yet |
| Subject Knowledge | `/app/ml/subjects` | Tracks a domain package and review status | Does not ingest its documents yet |
| Data Labeling | `/app/ml/label-tasks` | Tracks a target dataset, label mode, and task status | Does not yet store or edit the full label records |
| Data Preparation | `/app/ml/prep-jobs` | Tracks a target dataset, operation, and job status | Does not execute transformations |
| Training Jobs | `/app/ml/training-jobs` | Tracks project/model/dataset/method and lifecycle; **Train now** really trains a tabular model by gradient descent | Trains tabular models only — it does not fine-tune language models |
| Evaluation Lab | `/app/ml/evaluation-runs` | Tracks the candidate, dataset, category, and run status; **Evaluate now** really computes and stores metrics | Scores tabular models only — no LLM benchmark suites yet |
| Experiment Tracking | `/app/ml/experiments` | Relates a project, model, optional dataset, and lifecycle | Does not yet capture the full metrics/artifact bundle |
| Fine-Tuning | `/app/ml/fine-tuning-jobs` | Tracks base model, dataset, method, and job lifecycle | Does not train an adapter or modify weights |
| Model Builder | `/app/ml/model-builder-configs` | Tracks a proposed source type and design lifecycle | Does not construct or train the architecture |
| Instruction Training | `/app/ml/instruction-examples` | Tracks a target dataset, subject, and review lifecycle | Is currently scoped metadata, not the complete example body |
| Synthetic Data | `/app/ml/synthetic-records` | Tracks generation technique, dataset, and review status | Does not generate content |
| Embeddings and Vector Stores | `/app/ml/vector-stores` | Registers store identity, embedding model name, metric, and approval | Does not create embeddings or populate the index |
| Retrieval-Augmented Generation | `/app/ml/rag-configs` | Registers a search strategy, optional vector-store relationship, and approval | Does not execute retrieval, assemble grounded prompts, or test citations |
| Subject Examination | `/app/ml/subject-exams` | Tracks an exam for a registered subject package, its question format, and review lifecycle | Does not administer questions or compute scores |
| Hyperparameter Optimization | `/app/ml/hyperparameter-searches` | Tracks a search over a training job's settings, its strategy, and job lifecycle | Does not run trials or select a best result |
| Model Optimization | `/app/ml/model-optimizations` | Tracks an intended operation (quantization, pruning, ...) against a registered model | Does not transform any model artifact |
| Checkpoint Management | `/app/ml/checkpoints` | Tracks checkpoint records; Phase 56 training runs create them automatically with genuinely measured losses | Records measured losses, not restorable weight snapshots; no resume or promotion |
| Deployment Manager | `/app/ml/deployments` | Tracks an intended promotion of a registered model to an environment, with strategy and approval | Does not serve, monitor, or roll back anything |
| Model Comparison | `/app/ml/model-comparisons` | Names a baseline model, candidate model, and shared benchmark; **Compare now** really evaluates both and stores the measured winner | Compares trained tabular models only — no latency, safety, or blind response comparison for language models |

The order in Section 12 intentionally moves between these pages according to the actual development lifecycle, rather than simply following the sidebar order.

The five newest record types form a governed post-training chain around a training job and its resulting model. The diagram shows how the records relate; every arrow is an administrative reference today, not an automated hand-off:

```mermaid
flowchart LR
    TJ["Training Job record"] --> HS["Hyperparameter Search record<br/>(tunes the job's settings)"]
    TJ --> CP["Checkpoint records<br/>(active / pinned / archived)"]
    TJ --> MR["Model Registry entry"]
    MR --> MO["Model Optimization record<br/>(quantization, pruning, ...)"]
    SK["Subject Knowledge package"] --> SE["Subject Exam record<br/>(reviewer-approved)"]
    SE -. "future: gates approval" .-> MR
    MR --> DP["Deployment record<br/>(pending → approved)"]
    DP -. "future: executor promotes" .-> ENV["Environment<br/>(staging, production, ...)"]
```

Three different lifecycles appear in this chain, and each was chosen to match what the record *is*:

- **Reviewer workflow** (`draft → in_review → approved / rejected → archived`) — Subject Exams, like Instruction Examples, because an exam is authored content a person reviews.
- **Job lifecycle** (`draft → queued → running → completed / failed / canceled → archived`, eleven states) — Hyperparameter Searches and Model Optimizations, because both execute like training jobs once a real executor exists.
- **Resource approval** (`pending → approved / rejected`) — Deployments, like Vector Stores and RAG configurations, because a deployment is infrastructure awaiting authorization.
- **Retention lifecycle** (`active → pinned → archived`) — Checkpoints only. Nobody "approves" a checkpoint; an administrator keeps it, protects it from retention deletion (`pinned`), or archives it.

### 10.6 Status is state, not evidence

A lifecycle status answers, “What state does the administrative record claim?” Evidence answers, “What verifiably happened?” They must agree, but they are not interchangeable.

For example, a legitimate completed training job should eventually be supported by:

- executor identity and version;
- start and finish times;
- exact input dataset and base-model digests;
- full configuration and seed;
- resource and metric logs;
- checkpoints and output hashes;
- cancellation/failure history;
- the user or service responsible;
- audit records.

Until an executor attaches that evidence, manually changing a status to `completed` proves only that the record was changed. The Phase 56 tabular executor is the first place this evidence loop closes: a job it ran carries a real loss curve, real held-out metrics, real checkpoint losses, and a real weight artifact — and its statuses were set by the executor, not by hand.

### 10.7 Hands-on lesson: train, evaluate, and use a real tabular model

This is the first fully executable machine-learning workflow inside MasterAI. It uses the classic supervised-learning loop from Section 2 on your own CSV data. Everything in this lesson computes for real.

```mermaid
flowchart LR
    A["1. Register dataset<br/>(Dataset Manager)"] --> B["2. Upload CSV content<br/>(validated + profiled)"]
    B --> C["3. Create training job<br/>(Training Jobs)"]
    C --> D["4. Train now<br/>(gradient descent,<br/>real loss curve)"]
    D --> E["Model Registry entry<br/>+ persisted weights<br/>(state: evaluation)"]
    D --> F["Checkpoint records<br/>(measured losses)"]
    E --> G["5. Evaluate now<br/>(held-out or benchmark<br/>dataset, real metrics)"]
    G --> H["6. Predict<br/>(live, from persisted<br/>weights)"]
    E --> I["7. Compare now<br/>(baseline vs candidate<br/>on a shared benchmark)"]
```

**Step 1 — Prepare a CSV the engine can learn from.** The first row is the header. Every feature column must be numeric (measurements, counts, encoded flags). The target column — what the model should learn to predict — may be numeric (regression: price, temperature, duration) or categorical text (classification: `spam`/`ham`, `low`/`medium`/`high`, species names). At least one feature column, one target column, and two data rows are required; up to 8 MiB and 64 distinct class labels are accepted.

**Step 2 — Register the dataset and upload its content.** In Dataset Manager, register the dataset as before, then use **Upload dataset content (CSV)** with the dataset's ID, the target column name (blank means the last column), and the CSV text. The server parses everything before storing anything; a malformed row or a non-numeric feature cell is rejected with the exact row and column named. A successful upload reports the real profile: row count, feature columns, and the detected task.

**Step 3 — Create a training job for that dataset.** In Training Jobs, create a job whose Dataset ID is the dataset you just filled. The Model ID may be left blank — the executor will register the trained model for you.

**Step 4 — Train.** Click **Train now**. The executor moves the job through `queued → preparing → running` for real, runs full-batch gradient descent (default: 200 epochs, learning rate 0.05, 20% held-out split, seed 42 — all overridable through `POST /api/v1/ml/training-jobs/{id}/run`), and reports the method it chose, the final loss, and the held-out metrics in the result panel. It also creates checkpoint records carrying the actually measured losses, persists the learned weights, and leaves the model registry entry in the `evaluation` state with the job at `awaiting_evaluation` — the honest place for an unreviewed model.

**Step 5 — Evaluate like a professional.** Held-out metrics from training are a good first signal, but Section 9 taught that evaluation should be an independent act. Register a second benchmark dataset with the same columns, upload its content, create an Evaluation Run naming the trained model and the benchmark dataset, and click **Evaluate now**. The harness computes real metrics — accuracy, macro precision/recall/F1, and a confusion matrix for classification; MSE, MAE, and R² for regression — and stores them permanently behind **View result**.

**Step 6 — Predict.** In Model Registry, use **Predict with a trained model**: enter the trained model's ID and the feature values as a JSON object keyed by column name (for example `{"sepal_length":5.1,"sepal_width":3.5}`). Classification returns the winning label with every class probability; regression returns the predicted value. Because the weights are persisted, this works across server restarts.

**Step 7 — Compare two models like a scientist.** Section 9 also taught that a single score means little without a controlled comparison. Train a second model — a different feature set, different hyperparameters, or newer data — then open **Model Comparison**, create a comparison naming the current model as the baseline, the new model as the candidate, and one shared benchmark dataset (both models must have been trained, and the benchmark must have uploaded content matching both schemas), and click **Compare now**. The harness evaluates *both* trained artifacts against the *same* rows — the only fair test — and stores a verdict: each model's full metrics, the primary-metric delta (macro F1 for classification, MSE for regression), and the measured winner. A tie is reported as a tie. This is exactly the controlled experiment a professional would run before replacing a production model, and the stored result behind **View result** is the evidence for that decision.

**What to watch for, in classroom terms.** If the final loss barely moves, the learning rate may be too small or the features uninformative. If training diverges (the run fails with a non-finite loss), lower the learning rate. If held-out accuracy is far below training accuracy, you are overfitting — Section 6.5 applies here exactly as it does to large models. And a constant feature column contributes nothing: the engine standardizes it to zero and its weight never moves.

---

## 11. Sequential lesson: set up an existing model for MasterAI

This is the current, operational path. Perform the steps in order.

### Step 1 — Define the intended use

Write down:

- task and users;
- data classification;
- required model capabilities;
- acceptable license;
- minimum quality;
- maximum memory and latency;
- whether citations or current knowledge are needed.

If the need is only current repository knowledge, begin with project context rather than training.

### Step 2 — Confirm the host and storage

Confirm CPU features, available RAM, optional GPU support and VRAM, free model storage, runtime storage, and expected context length. Leave an operating-system safety reserve. Do not size only for the model file; allow for mapped weights, runtime buffers, the KV cache, prompt processing, and concurrent requests.

MasterAI's authoritative project-owned model root is `models/`. The configured `workspace.modelsRoot` determines the runtime location. Relative paths resolve against the directory containing `settings.json`, so confirm the resolved value with:

```powershell
.\build\Windows-x64\Release\masterai.exe models-root .\config\settings.json
```

#### Understand the memory budget

The model file size is only the beginning of the memory estimate. Plan for:

```text
total working memory ≈ resident model weights
                     + KV cache
                     + runtime work buffers
                     + prompt and output buffers
                     + concurrent-slot overhead
                     + MasterAI and operating-system reserve
```

Approximate unquantized weight storage begins with:

```text
parameter count × bytes per parameter
```

For example, three billion parameters at two bytes each is roughly six billion bytes before format overhead and runtime allocations. Quantization can reduce weight storage, but actual GGUF size and measured resident memory are the values to trust.

The KV cache generally grows with context length, model architecture, cache precision, and the number of simultaneously reserved slots. Doubling context can therefore add substantial memory even though the model file did not change. MasterAI's admission controls may correctly reject a model/context/slot combination that would leave too little RAM or VRAM for safe operation.

#### Understand the compute budget

Consider:

- prompt-evaluation speed, because large project context must be processed before output begins;
- generated tokens per second;
- model cold-load time;
- CPU instruction support and thread count;
- optional GPU backend compatibility and usable VRAM;
- storage read performance and page-fault behavior;
- expected concurrency;
- power and thermal limits for long jobs.

Training usually needs far more memory than inference because it stores gradients, optimizer state, activations, and checkpoints. The fact that a quantized model runs in MasterAI does not prove the same host can fine-tune the source model.

### Step 3 — Obtain an approved inference runner

MasterAI does not vendor or build the inference backend. Obtain the approved, version-pinned `llama-server.exe` and record its exact absolute path. The current plan pins `llama.cpp` as the sole optional exception to the independently authored C++17 foundation.

Treat the executable as code:

- obtain it from an approved source;
- pin its revision;
- verify its digest;
- keep it outside source control;
- do not replace it silently after calibration or evaluation.

### Step 4 — Choose a compatible model artifact

The current inference adapter requires GGUF and `llama-cpp`. Select a model whose architecture is supported by the pinned runner and whose license, task, context capacity, and hardware requirements fit the use case.

Approved initial categories are:

- `general-programming`;
- `code-completion`;
- `code-review`;
- `debugging`;
- `documentation`;
- `embeddings-code-search`.

Place one model in:

```text
models/<category>/<model-id>/
├── manifest.json
└── <model-file>.gguf
```

### Step 5 — Create the strict manifest

Use [manifest.schema.json](../models/manifest.schema.json) as the authority. A simplified example is:

```json
{
  "schemaVersion": 1,
  "id": "my-cpp17-assistant-v1-q4km",
  "displayName": "My C++17 Assistant v1",
  "category": "general-programming",
  "model": {
    "format": "gguf",
    "architecture": "<actual-architecture>",
    "quantization": "Q4_K_M"
  },
  "requirements": {
    "minimumRamMiB": 4096,
    "recommendedRamMiB": 8192,
    "estimatedDiskMiB": 4096
  },
  "files": [
    {
      "path": "my-cpp17-assistant-v1-q4_k_m.gguf",
      "sizeBytes": 1234567890,
      "sha256": "<64-lowercase-hex-characters>"
    }
  ],
  "backends": ["llama-cpp"],
  "provenance": {
    "sourceUrl": "https://huggingface.co/<owner>/<repository>/resolve/<immutable-revision>/<file>",
    "revision": "<immutable-revision>"
  },
  "license": {
    "spdx": "Apache-2.0",
    "accepted": true
  },
  "hardware": {
    "cpuFeatures": [],
    "gpuBackend": ""
  }
}
```

Never copy placeholder values into a real manifest. Measure the exact file size, calculate the real SHA-256, use the real architecture, record an immutable source revision, and confirm that the SPDX value is accepted by the schema and valid for that model.

### Step 6 — Build MasterAI if necessary

From the repository root on Windows x64:

```powershell
.\scripts\build.ps1 -Platform Windows-x64 -BuildType Release
.\scripts\test.ps1 -Platform Windows-x64 -BuildType Release
```

This guide does not instruct you to run Linux or WSL validation as part of a normal Windows model setup. Follow the project rules and perform Linux work only when it is explicitly required and authorized.

### Step 7 — Configure MasterAI

```powershell
.\scripts\configure.ps1 -BuildType Release
```

In the wizard, confirm:

1. MasterAI's loopback service port.
2. Runtime data directory.
3. Model directory.
4. Absolute path to the approved `llama-server.exe`.
5. Optional approved `curl` path if MasterAI will perform downloads.
6. Allowed local-password and operating-system identity methods.

Leaving the inference executable blank disables inference.

### Step 8 — Verify the model

MasterAI does not trust discovery alone. Refresh the verification cache after adding, replacing, or removing any model artifact:

```powershell
.\scripts\rehash.ps1 -BuildType Release
```

Or call the binary directly:

```powershell
.\build\Windows-x64\Release\masterai.exe verify-models .\models
```

Verification checks path containment, manifest fields, file type, exact size, SHA-256, backend compatibility, licensing/provenance policy, and hardware suitability. A present but unverified file is not Ready.

### Step 9 — Start the service and create the first administrator

```powershell
.\scripts\start.ps1 -BuildType Release -Foreground
```

On first start, securely retain the one-time administrator token from the log. Open:

```text
http://127.0.0.1:7070
```

Use the setup form to create the first administrator. The Machine Learning interface is administrator-only.

### Step 10 — Confirm service and model state

Check:

```text
GET /health/live
GET /health/ready
```

Remember that `/health/ready` means the service and authentication path are ready; it is not proof that a model is verified or loaded. Confirm the model is shown as Ready in the model inventory before chat testing.

### Step 11 — Run a reproducible benchmark

```powershell
.\build\Windows-x64\Release\masterai.exe benchmark-model `
  .\config\settings.json `
  my-cpp17-assistant-v1-q4km `
  quick
```

Then use `standard` and `extended` when warranted. Compare only results produced with compatible hardware identity, suite hash, and profile. Record quality, latency, token rate, peak runner memory, and failures.

### Step 12 — Test in chat with controlled context

Create an ordinary MasterAI project if repository context is required. Create a chat, select the verified model, and test:

- simple subject questions;
- representative tasks;
- edge and ambiguous cases;
- unsafe or prohibited requests;
- long-context behavior;
- cancellation and timeouts;
- answers with and without project evidence.

Do not promote a model because one demonstration looks convincing.

### Step 13 — Record approval and operating limits

Record the approved model identity, hash, source, license, benchmark evidence, intended use, known limitations, context limit, reply limit, hardware profile, and rollback model. Only then permit controlled production use.

---

## 12. Sequential lesson: build and teach a specialized model

This is the recommended complete lifecycle. At the present implementation boundary, MasterAI can organize much of the control-plane metadata and can run the final compatible GGUF, while the actual training, conversion, and some evaluation work must occur in a separately approved workflow until the planned executors exist.

### Step 1 — Create the capability contract

Define exactly what the model must do, must not do, and how success will be measured. Name the target input, output, subject, users, security class, latency, memory, and quality limits.

**Exit condition:** The requirement is testable and approved.

### Step 2 — Establish a baseline

Choose one or more approved base models and run the evaluation set before teaching. Preserve prompts, runtime settings, model hashes, hardware identity, and results.

**Exit condition:** You know the current quality and cost.

### Step 3 — Create a Machine Learning project

Sign in as an administrator and open:

```text
/app/ml/projects
```

Create a project with a clear name, objective, subject, task, owner, and initial `draft` status. The current record is a scoped organizational container; preserve additional requirements in approved project documentation until the full planned fields are implemented.

**Exit condition:** The development objective has one authoritative project identity.

### Step 4 — Choose the teaching method

Apply the decision tree in Section 7:

- prompt first for simple behavioral instruction;
- project context or RAG for changing knowledge and citations;
- fine-tuning for stable repeated behavior, terminology, format, or tool patterns;
- combine RAG and fine-tuning when both behavior and current facts matter;
- avoid foundation training unless a separately justified program proves it necessary.

If RAG is selected, the current administration foundation can register the intended configuration at:

```text
/app/ml/rag-configs
```

Record the search strategy and, where applicable, its registered vector store. The current Phase 50 record and approval lifecycle do not execute query rewriting, retrieval, reranking, context assembly, citation checking, or grounding tests. Specify and validate those behaviors in a controlled external workflow until the planned executor exists.

**Exit condition:** The method is justified against simpler alternatives.

### Step 5 — Register the base model and its provenance

Use the Machine Learning Model Registry at:

```text
/app/ml/models
```

Record the model name, source, task, and lifecycle state supported by the current interface. Separately retain full provenance: exact version, architecture, tokenizer, template, license, hash, signature if available, hardware/runtime requirements, and base evaluation.

**Exit condition:** The exact base model can be reproduced and legally used.

### Step 6 — Create the subject knowledge package

Open:

```text
/app/ml/subjects
```

Define the subject, scope, owner, and review lifecycle. In the accompanying controlled subject specification, include terminology, concepts, rules, procedures, examples, counterexamples, sources, prohibited conclusions, known limits, evaluation questions, and update schedule.

**Exit condition:** A reviewer can tell what is inside and outside the subject.

### Step 7 — Acquire and register approved data

Open:

```text
/app/ml/datasets
```

Register the intended dataset and its source. Do not approve it yet. Build a provenance inventory for every source and reject data with unresolved license, consent, integrity, or classification issues.

**Exit condition:** Every source has an owner, permitted use, classification, revision, and checksum.

### Step 8 — Prepare and label the data

Use the current administrative records at:

```text
/app/ml/prep-jobs
/app/ml/label-tasks
/app/ml/instruction-examples
/app/ml/synthetic-records
```

These pages currently track scoped jobs, tasks, and examples; they do not yet execute the full preparation or labeling pipeline. Perform the actual transformations in an approved, isolated, reproducible environment. Keep scripts, configuration, logs, input hashes, output hashes, and reviewer decisions.

Required order:

1. Verify and scan inputs.
2. Normalize encoding and schema.
3. Detect and remove secrets and personal information according to policy.
4. Remove duplicates before splitting.
5. Label against written guidelines.
6. Review machine-generated labels.
7. Mark synthetic examples clearly.
8. Split by leakage-safe groups.
9. Freeze immutable training, validation, and test versions.
10. Approve only after quality and isolation checks pass.

**Exit condition:** The immutable dataset version passes the checklist in Section 8.7.

### Step 9 — Design the experiment

Open:

```text
/app/ml/experiments
/app/ml/model-builder-configs
/app/ml/hyperparameter-searches
```

If the experiment includes a settings search rather than one fixed configuration, register a Hyperparameter Search against the training job it tunes and record the intended strategy (for example grid, random, or Bayesian). The current record tracks identity, target job, strategy, and job lifecycle only — it does not run trials, so the search space, per-trial results, and best-configuration selection must live in the experiment specification until the planned executor exists.

Record the project, model, dataset, and intended lifecycle. In the full experiment specification, pin:

- source-code revision;
- training runtime and dependency versions;
- base model and tokenizer digests;
- dataset version and checksum;
- method and trainable layers;
- hyperparameters and random seed;
- compute target and resource ceilings;
- checkpoint and cancellation policy;
- metrics and approval thresholds;
- output naming and storage;
- network and secret policy.

Change one meaningful variable at a time for early experiments when practical.

**Exit condition:** Another authorized trainer could repeat the experiment.

### Step 10 — Run a small pilot before a full job

Use a small, representative training subset to confirm:

- the data loader works;
- losses are finite and move sensibly;
- checkpoints can be written and restored;
- validation is independent;
- memory, disk, and thermal behavior stay within limits;
- cancellation works;
- logs contain no secrets or raw restricted data.

Do not spend the full compute budget until the pipeline passes this pilot.

**Exit condition:** The pipeline is operational and bounded.

### Step 11 — Create and execute the real training or fine-tuning job

Record the administrative job at one of:

```text
/app/ml/training-jobs
/app/ml/fine-tuning-jobs
```

The present MasterAI job record does not launch a training framework. Execute the approved job outside MasterAI's current runtime boundary, or wait for the planned isolated training executor. Never imitate execution by merely advancing the stored job status.

Monitor training and validation loss, learning rate, gradient norm, throughput, CPU, GPU, memory, disk, temperature, warnings, and checkpoint health. Stop on NaN/Inf loss, uncontrolled resource use, data-policy failure, worsening validation beyond policy, or loss of reproducibility evidence.

As the external workflow produces checkpoints, register the ones worth governing at:

```text
/app/ml/checkpoints
```

Each record names the training job that produced it and why it was captured (for example `epoch_end`, `best_metric`, or `manual`). Set a checkpoint to `pinned` to declare it exempt from retention deletion — the candidate you intend to evaluate and the last-known-good state are the usual candidates — and `archived` when it is retired. The record governs retention intent; the checkpoint file itself, its step/epoch metadata, and its hash remain in the external workflow's evidence bundle until the planned executor captures them directly.

**Exit condition:** A recoverable candidate checkpoint and complete evidence bundle exist.

### Step 12 — Evaluate the candidate

Create an evaluation record at:

```text
/app/ml/evaluation-runs
```

If the candidate must pass a subject-specific examination — the plan's Subject Examination System — register the exam against its subject knowledge package at:

```text
/app/ml/subject-exams
```

Record the exam's question format (for example `multiple_choice`, `short_answer`, or `code_task`) and move it through review to `approved` before administering it. The current record establishes which exam exists, which subject it examines, and whether a reviewer approved it; the question bank, administration, and computed scores remain in the external evaluation workflow until the planned executor exists.

Run the frozen test set only after model and hyperparameter choices are settled. Compare against the base model and previous approved version. Include target capability, regressions, safety, adversarial cases, privacy leakage, memorization probes, latency, memory, and hardware compatibility.

**Exit condition:** All mandatory thresholds pass, or the candidate is rejected with recorded reasons.

### Step 13 — Review errors and iterate

Classify failures by topic and cause:

- missing or bad data;
- ambiguous instruction;
- incorrect label;
- retrieval failure;
- model capacity limit;
- overfitting;
- catastrophic forgetting;
- output-format failure;
- safety-policy failure;
- runtime or quantization regression.

Create a new dataset or experiment version. Never rewrite the evidence of the failed run.

**Exit condition:** The next iteration is based on diagnosed evidence, not guesswork.

### Step 14 — Convert and optimize for MasterAI inference

After approval of a source checkpoint, convert or merge it through an approved isolated process into a `llama.cpp`-compatible GGUF. Select quantization using measured quality and hardware results, not file size alone.

Register the intended operation against the registered model at:

```text
/app/ml/model-optimizations
```

Name the operation honestly (for example `quantization`, `pruning`, or `distillation`) and advance the record through the job lifecycle as the external workflow actually runs. The record does not transform the artifact — it creates the governed identity that the before/after quality comparison and the resulting artifact's hash will be attached to once the planned optimizer executor exists.

Treat each conversion or quantization as a new candidate artifact with a new hash and evaluation. A model that passed before conversion may fail afterward.

**Exit condition:** The GGUF is compatible, reproducible, licensed, hashed, and separately evaluated.

### Step 15 — Package the model in MasterAI

Create:

```text
models/<approved-category>/<unique-model-id>/
├── manifest.json
└── <unique-model-file>.gguf
```

Never overwrite the previous production directory. Use a new model ID and version. Complete the manifest with exact metadata, then run `verify-models` or `rehash.ps1`.

**Exit condition:** MasterAI reports the model Ready without weakening a check.

### Step 16 — Benchmark on the target host

Run the same MasterAI benchmark profile against the base, previous production, and candidate models on the same host. Also run the subject-specific evaluation externally until MasterAI's complete evaluation harness exists.

**Exit condition:** Quality, latency, memory, and stability meet the deployment contract.

### Step 17 — Deploy to staging

Load the candidate only in a controlled staging environment. Test real prompt assembly, project authorization, context limits, cancellation, concurrent requests, log redaction, and expected integrations.

**Exit condition:** Staging evidence matches offline evaluation and no policy boundary is bypassed.

### Step 18 — Approve production deployment

Require the designated model evaluator, safety reviewer, and deployment authority. For high-risk use, require dual approval. Record the model card, dataset card, evaluations, known limits, monitoring thresholds, and rollback model.

Register the promotion itself at:

```text
/app/ml/deployments
```

Name the registered model, the target environment (for example `staging` or `production`), and the strategy (for example `direct`, `blue_green`, or `canary`). A new deployment starts `pending`; the deployment authority moves it to `approved` as the recorded act of authorization, or `rejected` with reasons. The record is the auditable approval — it does not load the model, route requests, or roll anything back. Serving still happens through the verified inference model tree (Section 10.3), and the runtime/target-node/health-status evidence will attach to this record once the planned deployment executor exists.

**Exit condition:** Approval is explicit, attributable, and auditable.

### Step 19 — Monitor production

Monitor:

- input and output drift;
- subject accuracy and user rejection;
- hallucination and citation failures;
- unsafe outputs;
- latency, timeouts, memory, and load time;
- retrieval and tool-use failures;
- changes in data or policy;
- integrity of runner and model files.

Feedback must enter a review queue. Do not train automatically from conversations.

**Exit condition:** The model stays within its approved quality, safety, and resource envelope.

### Step 20 — Version, roll back, or retire

Rollback when a safety, integrity, quality, or resource threshold is breached. Preserve the failed artifact and evidence according to policy; do not silently delete history. Create a new version for corrected behavior and repeat the gates.

**Exit condition:** Production always points to an approved, recoverable version.

---

## 13. Worked classroom example: a C++17 subject assistant

This example shows how the lessons join together.

### 13.1 Objective

Build an assistant that reviews C++17 code for resource ownership, path safety, error handling, and concurrency defects while respecting MasterAI's independent C++17 architecture.

### 13.2 First attempt: no training

1. Select a verified programming model.
2. Create a MasterAI project for the authorized repository.
3. Index the project.
4. Provide a focused system instruction and review rubric.
5. Ask representative questions with current file context.
6. Score correctness, compilability, evidence use, and false positives.

If this meets the threshold, stop. Training would add cost and risk without proven benefit.

### 13.3 Add retrieval when current rules matter

Keep project rules, architecture decisions, and current source separately managed. Retrieve the relevant passages for each review. This is better than attempting to encode frequently changing project facts into weights.

### 13.4 Fine-tune only for persistent review behavior

If the base model repeatedly ignores the required report format or misses stable C++17 patterns despite good context, create reviewed examples:

```json
{
  "instruction": "Review the function for ownership and path-safety defects.",
  "context": "<self-contained C++17 example>",
  "expectedResponse": "<evidence-linked finding or explicit no-finding result>",
  "subject": "C++17 secure code review",
  "difficulty": "medium",
  "reviewStatus": "approved"
}
```

Include clean examples so the model learns not to invent findings. Put variants from the same defect family into one data partition to prevent leakage.

### 13.5 Evaluation rubric

Measure:

- true defects found;
- false-positive rate;
- severity calibration;
- accurate source references;
- C++17 compliance;
- compilability of proposed changes;
- preservation of unrelated behavior;
- refusal to invent absent evidence;
- latency and memory;
- regression on general programming tasks.

### 13.6 Deployment decision

Approve only if the specialized model beats the base model on the frozen subject test without unacceptable regressions. Package the evaluated GGUF under a new model ID, verify its SHA-256, benchmark it in MasterAI, stage it, and preserve the previous approved model for rollback.

---

## 14. Security, privacy, and governance rules

### 14.1 Treat models and datasets as sensitive assets

Model weights may memorize or reveal training content. Dataset deletion does not automatically erase information learned into an already trained model. Data-removal obligations may require retraining or retiring derived models.

### 14.2 Least privilege

Training workloads must not automatically receive:

- administrator privileges;
- unrestricted filesystem access;
- unrestricted network access;
- production credentials;
- access to unrelated projects;
- database-administrator credentials.

### 14.3 Untrusted model code

Some model packages require custom executable code. Do not execute that code automatically. Inspect, approve, pin, and isolate it. MasterAI's current inference package deliberately permits only the strict GGUF and `llama.cpp` path.

### 14.4 Poisoning and prompt injection

Imported documents and feedback can contain malicious instructions. Treat source content as data, not authority. Preserve source boundaries, filter and classify content, apply access control before retrieval, and never let retrieved text override system policy.

### 14.5 Reproducibility and audit

Every result should be traceable to:

```text
user + project + source code + configuration + base model + tokenizer
+ dataset version + seed + runtime + hardware + checkpoints + evaluation
```

Record state transitions and their reasons. Do not convert a status field into evidence that an operation happened.

### 14.6 Production model card

Every production model should document:

- purpose and owner;
- intended and prohibited uses;
- base model and lineage;
- training and evaluation data references;
- license and provenance;
- evaluation and safety results;
- known limitations;
- input and output types;
- hardware and runtime requirements;
- model and tokenizer hashes;
- approval status and approvers;
- monitoring and rollback rules.

---

## 15. Troubleshooting guide

| Symptom | Likely cause | Correct response |
|---|---|---|
| Model file exists but is not Ready | Verification cache missing, manifest mismatch, unsupported backend, or unsuitable hardware | Run `verify-models`; correct the cause rather than bypassing verification |
| `inference_backend_not_configured` | `llamaServerExecutable` is blank or invalid | Re-run configuration with the approved absolute runner path |
| `model_not_ready` | Model has not passed all readiness checks | Inspect manifest, exact size, hash, license, backend, and hardware assessment |
| Model loads but replies are poor | Unsuitable base model, prompt/template mismatch, quantization loss, or weak evaluation assumptions | Compare baseline, inspect template and settings, and test another approved candidate |
| Training loss falls but test quality does not | Overfitting, leakage, or objective mismatch | Stop, inspect splits and labels, revise data or regularization |
| Fine-tuned model forgets general skills | Catastrophic forgetting | Reduce update scope/rate, use an adapter, improve mixed data, and enforce regression tests |
| RAG answers cite irrelevant material | Weak chunking, retrieval, filtering, threshold, or reranking | Evaluate retrieval separately before blaming generation |
| A Training Job says `completed` but no artifact exists | Current record status was advanced without a real executor | Treat it as invalid operational evidence; run a controlled external job or wait for the executor |
| Vector Store exists but searches return nothing | Current scoped record does not populate an index | Implement or use an approved embedding/index pipeline; do not infer readiness from registration |
| Deployment is `approved` but the model is not being served | The deployment record is authorization, not execution | Package and verify the GGUF in the inference model tree; the record governs the promotion, it does not perform it |
| Checkpoint record exists but no checkpoint file can be found | The record tracks retention intent, not the saved weights | Locate the file in the external workflow's evidence bundle; treat an unbacked record as a bookkeeping error to correct |
| Hyperparameter Search shows `completed` with no trial results | Status was advanced without a real search executor | Keep trial evidence in the experiment specification; treat the status as intent until the executor exists |
| Larger context causes load rejection or severe slowdown | KV-cache and runtime memory exceed the safety ceiling | Reduce context/slots, choose a smaller model or quantization, and recalibrate |
| Quantized model is faster but less accurate | Precision reduction changed behavior | Evaluate each quantization and reject unacceptable quality loss |

---

## 16. Glossary of essential terms

| Term | Classroom definition |
|---|---|
| Activation function | A nonlinear transformation applied inside a neural network so it can represent more than linear relationships. |
| Adapter | A small trainable module attached to a base model to specialize it without updating every base weight. |
| Architecture | The design of the model's computation: layers, connections, dimensions, and operations. |
| Artifact | A produced file or record such as a model, adapter, checkpoint, dataset, log, or evaluation report. |
| Attention | A transformer operation that combines information from relevant token positions using learned query, key, and value projections. |
| Backpropagation | The algorithm that calculates gradients from the loss backward through a differentiable computation graph. |
| Base model | The existing model from which prompting, retrieval, or fine-tuning begins. |
| Batch | A group of examples processed together before an optimizer update. |
| Benchmark | A fixed, reproducible set of tasks and scoring rules used to compare systems. |
| Checkpoint | A saved training state that permits evaluation, recovery, or continued training. |
| Chunk | A bounded section of a document used for indexing, retrieval, or dataset construction. |
| Classification | Selecting a category or label for an input. |
| Context window | The maximum bounded token sequence a language model can consider for one inference sequence. |
| Data leakage | Information improperly crossing into training, validation, or test data and making results misleading. |
| Deployment strategy | The controlled method used to promote a model into an environment, such as direct replacement, blue-green switchover, or a canary rollout to a small share of traffic first. |
| Dataset | A governed collection of records used for training, validation, testing, evaluation, or retrieval. |
| Distillation | Training a smaller student model to reproduce selected behavior of a larger teacher model. |
| Embedding | A numeric vector representation used internally by models or for similarity search. |
| Epoch | One pass through the selected training dataset. |
| Evaluation | Measurement of a fixed model against defined quality, safety, and resource criteria. |
| Feature | Information supplied to a model as input. |
| Fine-tuning | Continuing training from an existing model to adapt behavior or task ability. |
| Foundation model | A broadly pre-trained model intended to support many downstream tasks. |
| Generalization | Useful performance on new examples not used to fit the model. |
| GGUF | An inference-oriented model file format used by MasterAI's current `llama.cpp` adapter. |
| Gradient | The estimated direction and sensitivity of loss with respect to a trainable parameter. |
| Hallucination | A plausible-looking model output that is unsupported, incorrect, or fabricated. |
| Hyperparameter | A trainer-selected setting such as learning rate, batch size, or epoch count. |
| Hyperparameter optimization | A systematic search (grid, random, Bayesian, and similar strategies) over training settings to find a configuration that performs best on validation data within resource limits. |
| Inference | Running a fixed trained model to obtain a prediction or generated output. |
| Instruction tuning | Fine-tuning on instruction-and-response examples to shape response behavior. |
| KV cache | Stored attention keys and values that avoid recomputing eligible earlier token positions. |
| Label | The desired target, category, score, span, response, or preference attached to a supervised example. |
| Learning rate | The scale of optimizer parameter updates. |
| Logit | A raw model score converted into a probability distribution for selection or loss calculation. |
| LoRA | Low-rank adaptation, a parameter-efficient method that learns compact updates to selected weights. |
| Loss | A numeric training objective measuring disagreement between prediction and target. |
| Model card | A governed description of model purpose, lineage, data, evaluation, limits, safety, and approval. |
| Model optimization | Post-training transformation of a finished model — quantization, pruning, distillation, merging, and similar operations — to reduce resource cost, always re-evaluated because quality can change. |
| Optimizer | The algorithm that converts gradients into parameter updates. |
| Overfitting | Learning training examples too specifically, causing weaker performance on unseen data. |
| Parameter or weight | A numeric value learned during training and used during inference. |
| Perplexity | A language-model metric related to how much probability the model assigns to observed tokens; lower is better only within a compatible comparison. |
| Prompt | Instructions and content supplied to a generative model for the current request. |
| Provenance | The traceable origin, revision, ownership, license, and transformation history of an asset. |
| Quantization | Representing weights or caches with reduced numeric precision to reduce resource use. |
| RAG | Retrieval-augmented generation: retrieving authorized external knowledge and adding it to the inference context. |
| Regression | Either predicting a numeric value or, in testing language, an unwanted loss of previously working behavior. |
| Reranker | A model or algorithm that reorders retrieved candidates by estimated relevance. |
| Seed | A value used to initialize controlled pseudorandom behavior for improved repeatability. |
| Step | Commonly one optimizer update during training. |
| Subject examination | A governed set of questions and scoring rules used to test whether a model has actually learned an approved subject to its required standard. |
| Synthetic data | Machine-generated examples kept distinguishable from human-created or real-world records. |
| Temperature | A generation setting that adjusts how concentrated or varied token selection is. |
| Token | A vocabulary unit produced by a tokenizer, such as a word fragment, symbol, or whitespace pattern. |
| Tokenizer | The exact algorithm and vocabulary that convert text into token identifiers and back. |
| Training | Optimizing trainable parameters against an objective using examples. |
| Validation set | Held-out development data used for model and hyperparameter decisions. |
| Vector store | A controlled index of embeddings and metadata used for similarity retrieval. |

---

## 17. Classroom review questions

Use these questions before beginning a real model project:

1. What is the difference between a parameter and a hyperparameter?
2. Why does adding context not permanently teach model weights?
3. When is RAG preferable to fine-tuning?
4. Why must duplicate families be separated before creating dataset splits?
5. Why is a falling training loss insufficient for approval?
6. What changes when a model is quantized?
7. Why must the tokenizer and chat template be part of model identity?
8. What does a MasterAI Training Job record prove today, and what does it not prove?
9. Why are the ML Model Registry and the inference `models/` tree different?
10. Which evidence must exist before a specialized model enters production?
11. Why does a Checkpoint record use a retention lifecycle instead of an approval workflow?
12. What does an `approved` Deployment record authorize, and what does it not do?

Expected answers:

1. Parameters are learned; hyperparameters control learning or execution.
2. Context affects only the current bounded inference input and cache.
3. Use RAG for current, private, separately managed, or citable knowledge.
4. Otherwise near-identical answers can leak into validation or test data.
5. It says only that the optimization objective improved on training data.
6. Numeric weight precision and runtime characteristics change; teaching does not occur.
7. They determine how text becomes tokens and how roles/content are presented.
8. It proves a persisted administrative record and status, not real framework execution.
9. One tracks development governance; the other is the verified deployable GGUF runtime tree.
10. Identity, provenance, license, dataset lineage, evaluation, safety, resource, approval, staging, monitoring, and rollback evidence.
11. A checkpoint is not judged good or bad on creation — it is kept, protected from retention deletion (`pinned`), or archived, so its states describe retention, not approval.
12. It records that the deployment authority authorized promoting that model to that environment with that strategy; it does not load, serve, monitor, or roll back the model.

---

## 18. Final sequential checklist

Use this as the authoritative order for a model-development exercise:

- [ ] Define the task, users, boundaries, risks, and measurable success criteria.
- [ ] Establish a base-model and prompt/context baseline.
- [ ] Create the MasterAI Machine Learning project.
- [ ] Choose prompting, project context, RAG, fine-tuning, or a justified combination.
- [ ] Register the exact base model and provenance.
- [ ] Define and approve the subject knowledge package.
- [ ] Acquire only approved, licensed, classified sources.
- [ ] Verify, clean, redact, deduplicate, label, and review data.
- [ ] Split immutable training, validation, and test sets without leakage.
- [ ] Pin code, configuration, runtime, hardware, seeds, metrics, and ceilings.
- [ ] Run a bounded pilot and prove cancellation/checkpoint recovery.
- [ ] Execute the approved training workflow; do not simulate it with status changes.
- [ ] Register governed checkpoints and pin the ones retention must never delete.
- [ ] Evaluate against the base model, regressions, safety tests, and resource limits.
- [ ] Diagnose failures and create new immutable versions for each iteration.
- [ ] Convert and quantize the approved checkpoint for `llama.cpp` only when required.
- [ ] Re-evaluate the final GGUF artifact.
- [ ] Create the strict MasterAI manifest and unique model directory.
- [ ] Verify the model hash and Ready state.
- [ ] Benchmark on the target host.
- [ ] Deploy to staging and validate the real MasterAI request path.
- [ ] Obtain explicit production approval and record it as an approved Deployment.
- [ ] Monitor drift, safety, quality, integrity, and resources.
- [ ] Preserve a tested rollback path and retire obsolete versions safely.

---

## 19. Closing lesson

Machine learning is controlled experimental engineering. A model learns statistical behavior from data and objectives; it does not automatically acquire truth, policy, or judgment. The safest and most efficient path is to begin with the smallest intervention that can satisfy the requirement: a clear prompt, then authorized context or retrieval, then fine-tuning only when persistent behavior must change.

MasterAI should be used as the governed boundary around that lifecycle. Its current strengths are verified local inference, process isolation, authorization, project context, model integrity, resource control, benchmarking, and increasingly detailed Machine Learning administration records. Its planned training system should extend those same principles into execution without weakening them.

The professional standard is therefore simple:

> Never call a model learned, approved, or production-ready because a file exists, a loss decreased, a status changed, or one answer looked good. Call it ready only when its identity, data lineage, measured behavior, safety, resource use, approvals, and recovery path are all proven.

## Related project documentation

- [Authoritative implementation plan](PLAN.md)
- [MasterAI model category taxonomy](architecture/model-taxonomy.md)
- [Model manifest schema](../models/manifest.schema.json)
- [Configuration field reference](architecture/configuration.md)
- [Phase 4–7 inference, download, and benchmark operations](operations/phase-4-7-operations.md)
- [Security threat model](security/threat-model.md)
- [Project objectives](objectives.md)
