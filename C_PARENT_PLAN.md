# Plan: Indexing C Designated Initializers

## Rationale
To improve structural quality analysis, we need to correctly associate C designated initializers (e.g., `.name = "..."`) with their parent struct types (e.g., `IndexerConfig`) in the code index. Currently, these fields are indexed but lack the `parent_symbol` metadata, hindering structural relationship mapping.

## Findings
- **Status**: Designated initializers are indexed as `PROP` (properties), but `parent_symbol` is empty.
- **Root Cause**: The current `tree-sitter` traversal in `c_language.c` treats designated initializers as declarative statements rather than expressions, failing to link the `field_identifier` (e.g., `name`) to the enclosing struct type (`IndexerConfig`).
- **Data Availability**: The AST provides the necessary nesting (`declaration` -> `type_identifier` + `initializer_list` -> `initializer_pair`).

### Verification Method
We can verify the AST structure using the `ast-explorer-c` tool:

```bash
./tools/ast-explorer-c ./python/index-python.c | grep IndexerConfig -C 10
```

This reveals the hierarchy:
```text
      declaration [67:4 - 74:6]
        type_identifier [67:4 - 67:17] "IndexerConfig"
        init_declarator [67:18 - 74:5]
          identifier [67:18 - 67:24] "config"
          ...
          initializer_list [67:27 - 74:5]
            initializer_pair [68:8 - 68:30]
              field_designator [68:8 - 68:13]
                . [68:8 - 68:9] "."
                field_identifier [68:9 - 68:13] "name"
              = [68:14 - 68:15] "="
```

## Planned Approach
1.  **Enhance Context Tracking**: Modify `handle_declaration` in `c/c_language.c` to extract the `type_identifier` (e.g., `IndexerConfig`) when processing an `init_declarator`.
2.  **Context Propagation**: Update the traversal to pass the identified struct type name down to the `initializer_list` processing logic.
3.  **Assign Metadata**: Modify the `initializer_pair` handler to associate the extracted field name with the declared variable name (`config`) as the `parent_symbol` in `add_entry`.
    * *Note (decision 2026-06-09)*: `parent_symbol` is always the **syntactic parent** as written in the source (`config`), never a resolved type (`IndexerConfig`) — consistent with how `config.name` field accesses are indexed. The type is deducible separately: the variable's VAR row carries `TYPE=IndexerConfig`. Nested lists follow the same convention: in `.limits = {.max = 10}`, `max` gets parent `limits`.
4.  **Verification**:
    - Re-index `python/index-python.c`.
    - Run SQL query: `SELECT symbol, parent_symbol, filename FROM code_index WHERE filename='index-python.c' AND context='PROP';`
    - Confirm `.name`, `.data_dir` etc., have `parent_symbol` set to `config`.
