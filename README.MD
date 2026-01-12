# Memory efficient epub parsing and layouting

Here is my work in progress code for https://github.com/CidVonHighwind/microreader/

Heap fragmentation is a bitch and why add features when you can waste your time with this.

Goal is to reduce memory fragmentation as much as possible. If we manage to save DRAM in the process, that's great too.

Extensive hole punching could be mitigated by separating memory with different lifespans and releasing those blocks at once or not at all.
- short lived "workmem"
    - parsing stuff like gzip buffers
    - entire file contents or segments
    - layout temporary buffers
- semi-permanent
    - actual file contents (TOC, Styles)
    - layout results for the duration of a book being open
- permanent
    - settings

Things that are always needed and never get moved might as well be static.

## Memory Usage Estimation (32-bit hardware)

### Structure Sizes (32-bit)

| Structure        | Size (bytes) | Notes                                                    |
|------------------|--------------|----------------------------------------------------------|
| ZipFileEntry     | 24           | StringView(8) + 3×uint32_t(12) + uint16_t(2) + padding(2)|
| TocEntry         | 12           | StringView(8) + uint16_t(2) + padding(2)                 |
| SpineEntry       | 12           | StringView(8) + uint16_t(2) + padding(2)                 |
| spineTocIndices  | 2 per entry  | Separate uint16_t array (UINT16_MAX = no toc entry)      |

Note: On 64-bit systems, sizes are larger due to 8-byte pointers.

ZIP files with more than 65535 entries are rejected. This allows using uint16_t for all indices.

### Memory Estimates for Various EPUB Sizes (32-bit)

| Zip Entries | Spine | TOC | ZipFileEntry | SpineEntry | TocEntry | TocIndices | Total   |
|-------------|-------|-----|--------------|------------|----------|------------|---------|
| 50          | 20    | 15  | 1,200 B      | 240 B      | 180 B    | 40 B       | 1.7 KB  |
| 100         | 40    | 30  | 2,400 B      | 480 B      | 360 B    | 80 B       | 3.3 KB  |
| 200         | 80    | 50  | 4,800 B      | 960 B      | 600 B    | 160 B      | 6.5 KB  |
| 500         | 150   | 100 | 12,000 B     | 1,800 B    | 1,200 B  | 300 B      | 15.3 KB |
| 1000        | 300   | 200 | 24,000 B     | 3,600 B    | 2,400 B  | 600 B      | 30.6 KB |

Note: These estimates exclude string data (file names, TOC labels, idrefs). String data is stored
separately in the allocator and varies based on path/label lengths. Typical string overhead is
~50-100 bytes per zip entry for file names.

Using uint16_t indices instead of uint32_t saves 2 bytes per index. Splitting tocEntryIndex into
a separate array avoids std::optional alignment bloat (8 bytes vs 2 bytes per entry).
