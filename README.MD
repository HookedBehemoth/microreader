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

| Structure     | Size (bytes) | Notes                                                    |
|---------------|--------------|----------------------------------------------------------|
| ZipFileEntry  | 20           | StringView(8) + 3×uint32_t(12) + uint16_t(2) + padding(2) → 24 aligned |
| TocEntry      | 12           | StringView(8) + uint32_t(4)                              |
| SpineEntry    | 20           | StringView(8) + uint32_t(4) + optional<uint32_t>(8)      |

Note: On 64-bit systems, sizes are larger due to 8-byte pointers: ZipFileEntry=32, TocEntry=24, SpineEntry=32.

### Memory Estimates for Various EPUB Sizes (32-bit)

| Zip Entries | Spine Entries | TOC Entries | ZipFileEntry | SpineEntry | TocEntry | Total   |
|-------------|---------------|-------------|--------------|------------|----------|---------|
| 50          | 20            | 15          | 1,200 B      | 400 B      | 180 B    | 1.8 KB  |
| 100         | 40            | 30          | 2,400 B      | 800 B      | 360 B    | 3.6 KB  |
| 200         | 80            | 50          | 4,800 B      | 1,600 B    | 600 B    | 7.0 KB  |
| 500         | 150           | 100         | 12,000 B     | 3,000 B    | 1,200 B  | 16.2 KB |
| 1000        | 300           | 200         | 24,000 B     | 6,000 B    | 2,400 B  | 32.4 KB |

Note: These estimates exclude string data (file names, TOC labels, idrefs). String data is stored
separately in the allocator and varies based on path/label lengths. Typical string overhead is
~50-100 bytes per zip entry for file names.

Using indices (uint32_t) instead of StringView pointers for TOC/Spine src fields saves 4 bytes per
entry on 32-bit (8 bytes on 64-bit), which adds up for large EPUBs.
