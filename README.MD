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
