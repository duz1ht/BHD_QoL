# Repository Instructions for LLM Agents

## Knowledge_Base is read-only

LLM agents such as Codex must not create, edit, move, rename, delete, format, or otherwise modify any file or directory under `Knowledge_Base/`.

This restriction includes generated files, build artifacts, dependency updates, and bulk or automated operations whose path set could include `Knowledge_Base/`. Before running a write, format, cleanup, or refactoring command, agents must scope it explicitly so that the directory is excluded.

Before committing changes, agents must verify that `git status --short -- Knowledge_Base/` has no output. If it reports a change, the agent must stop and leave restoration of the read-only content to a human; the agent must not use a modifying command to restore it.

The `Knowledge_Base/` directory is reserved as a read-only reference area. Its purpose is to store code, examples, or material extracted from other projects so agents and maintainers can study that knowledge and use it as implementation reference for the current project in the main repository tree.

When implementing changes, use `Knowledge_Base/` only for reading and analysis. Apply all actual project changes outside `Knowledge_Base/`, in the main repository files.
