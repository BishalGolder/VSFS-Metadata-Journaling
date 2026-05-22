# VSFS Metadata Journaling File System 🗄️

### Crash-Consistency Engine & Structural Validation for a Very Simple File System Layout

**Course:** CSE321: Operating Systems | BRAC University

---

## 📝 Project Overview

This project implements a custom **Metadata Journaling Layer** inside a simulated Very Simple File System (VSFS) to guarantee file-system crash consistency. Written in low-level C (`journal.c`), the software explicitly accesses and manipulates an 85-block raw binary disk image (`vsfs.img`). By enforcing a **Write-Ahead Logging (WAL)** protocol, the application prevents metadata corruption from structural runtime interrupts.

### The Problem it Solves:

If an OS system crash or power outage occurs while updating directories, modifying allocation bitmaps, or establishing file pointers, the disk image shifts into an unvalidated, corrupted state. This system prevents this by writing all metadata state transformations into a dedicated logging workspace first, ensuring atomic commitments to disk.

---

## 🧱 VSFS Disk Layout & Architecture

The filesystem interacts directly with an 85-block storage layer where each block spans exactly **4096 Bytes (4 KB)**. The logical configuration parameters are statically defined as follows:

```text
┌───────────────┬─────────────────┬────────────────┬────────────────┬───────────────┬─────────────────┐
│  Superblock   │ Journal Blocks  │  Inode Bitmap  │  Data Bitmap   │  Inode Table  │   Data Blocks   │
│   (Block 0)   │  (Blocks 1-16)  │   (Block 17)   │   (Block 18)   │ (Blocks 19-20)│  (Blocks 21-84) │
└───────────────┴─────────────────┴────────────────┴────────────────┴───────────────┴─────────────────┘
```

- **Superblock (Block 0):** Stores fundamental structural attributes.

- **Journal Space (Blocks 1-16):** A 16-block dedicated area used to append and parse transactional logs up to a capacity limit of \(16 \times 4096\) bytes.

- **Inode Bitmap (Block 17):** Tracks historical inode status indicators (Little-Endian structure).

- **Data Bitmap (Block 18):** Tracks allocated data blocks.

- **Inode Table (Blocks 19-20):** 128-byte raw structured blocks managing file type maps, link trackers, and size parameters.

- **Data Blocks (Blocks 21-84):** Contains raw directory records (`struct dirent`) and raw file payloads.

---

## ⚙️ Core Technical Implementation Details

Your implementation leverages distinct structures and procedural steps to maintain data integrity across operations:

### 1. Embedded Structural Protocol (`journal.c`)

- `struct journal_header`: Maintains log integrity by validating a unique `JOURNAL_MAGIC` signature (`0x4A524E4C`) and tracking spatial consumption via `nbytes_used`.

- Packed Transactions (`__attribute__((packed))`): Forces exact memory alignment for `struct data_record`, pairing a `rec_header` (`REC_DATA` / `REC_COMMIT`) with a targeted block address vector (`block_no`) and an explicit 4096-byte data buffer payload.

### 2. Transactional Appends (`./journal create <filename>`)

- **Bitmask Scan Allocation:** Sequentially scans Block 17 at a bitwise level to locate the first available free slot marker, updating the state using bitwise OR shift masks (`ibitmap[i/8] |= (1 << (i%8))`).

- **Multi-Block Inode Invariant:** Accommodates boundary overflows by identifying if a new file's descriptor maps to index block 19 or index block 20 (`Inode_table_starting + (inum / inodes_per_block)`). If a secondary block boundary is crossed, it reads the primary block, updates the root directory's dimensional size pointer, and queues both modifications into the log.

- **Atomic Log Commitment:** Packages and appends data snapshots (Bitmap, Inode Table block, and Root Directory) into the log sequentially before finalizing the operation by stamping an atomic `REC_COMMIT` marker.

### 3. Log Replays and Checkpoints (`./journal install`)

- **Transaction Isolation:** Scans past the fixed header and aggregates all sequential uncheckpointed data records (`REC_DATA`) into a temporary localized cache buffer array (`txn[64]`).

- **Atomic Recovery Execution:** Traverses the cache to verify structural records. Upon encountering a clean `REC_COMMIT` tag, it executes sequential, low-level disk writes (`write()`) to commit updates to the main file system structures.

- **Circular Ledger Clearing:** Resets storage consumption boundaries (`nbytes_used = sizeof(struct journal_header)`) to make the space available for future operations.

---

## 📁 Repository Directory Layout

```text
vsfs-metadata-journaling/
│
├── documentation/
│   └── Term Project_ Metadata Journaling.pdf
│       # Academic project specifications sheet
│
└── src/
    ├── journal.c
    │   # Metadata journaling engine (WAL & checkpoint loop)
    ├── mkfs.c
    │   # Disk initialization engine
    └── validator.c
        # Consistency inspector and validation engine
```

---

## 🛠️ Technology Stack

- **Programming Language:** C (Low-Level Systems Architecture)

- **System API Calls:** POSIX Kernel File System Frameworks (`open`, `read`, `write`, `lseek`)

- **Byte Management Engine:** Little-Endian bit manipulation and structural mapping

- **Development Toolkit:** GCC Compiler Toolchain & Linux Core Architecture (Ubuntu)