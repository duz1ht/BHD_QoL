# Repository Instructions for LLM Agents

## Knowledge_Base is read-only

LLM agents such as Codex must not create, edit, move, rename, delete, format, or otherwise modify any file or directory under `Knowledge_Base/`.

The `Knowledge_Base/` directory is reserved as a read-only reference area. Its purpose is to store code, examples, or material extracted from other projects so agents and maintainers can study that knowledge and use it as implementation reference for the current project in the main repository tree.

When implementing changes, use `Knowledge_Base/` only for reading and analysis. Apply all actual project changes outside `Knowledge_Base/`, in the main repository files.
