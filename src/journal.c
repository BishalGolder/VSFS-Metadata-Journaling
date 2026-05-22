#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>

//Defining values
#define Blk_size 4096
#define Journal_Block 1
#define Inode_bitmap_block 17
#define Data_bitmap_block 18
#define Inode_table_starting 19
#define Root_directory_block 21

//Magic number,rec types
#define JOURNAL_MAGIC 0x4A524E4C
#define REC_DATA 1
#define REC_COMMIT 2

//Structures
struct journal_header {
    uint32_t magic;
    uint32_t nbytes_used;
};

struct rec_header {
    uint16_t type;
    uint16_t size;
};

struct data_record {
    struct rec_header hdr;
    uint32_t block_no;
    uint8_t data[Blk_size];
} __attribute__((packed));

struct inode {
    uint16_t type;
    uint16_t links;
    uint32_t size;
    uint32_t direct[8];
    uint32_t ctime;
    uint32_t mtime;
    uint8_t _pad[128 - (2+2+4 + 8*4 + 4+4)];
};

struct dirent {
    uint32_t inode;
    char name[28];
};

//Writing journal header
void init_journal_on_need(int fd) {
    struct journal_header jh;
    lseek(fd, Journal_Block * Blk_size,SEEK_SET);
    read(fd, &jh, sizeof(jh));

    if (jh.magic != JOURNAL_MAGIC) {
        printf("Initializing journal header\n");
        jh.magic = JOURNAL_MAGIC;
        jh.nbytes_used = sizeof(struct journal_header);
        lseek(fd,Journal_Block * Blk_size, SEEK_SET);
        write(fd, &jh, sizeof(jh));
    }
}

// Appending record to the journal
void append_record(int fd, void* record, uint16_t size) {
    struct journal_header jh;
    lseek(fd, Journal_Block * Blk_size, SEEK_SET);
    read(fd, &jh, sizeof(jh));

    // Check if journal has space
    if (jh.nbytes_used + size > 16 * Blk_size) {
        fprintf(stderr, "Journal is full. Run Install to clear journal.\n");
        exit(1);
    }

    //writing record at the end of journal
    lseek(fd, (Journal_Block * Blk_size) + jh.nbytes_used, SEEK_SET);
    write(fd, record, size);

    //updating nbytes-used
    jh.nbytes_used += size;
    lseek(fd, Journal_Block * Blk_size, SEEK_SET);
    write(fd, &jh, sizeof(jh));
}

int create(const char* filename) {
    int fd = open("vsfs.img", O_RDWR);
    if (fd<0) {
        perror("vsfs.img could not be opened"); 
        return 1; 
    }
    init_journal_on_need(fd);

    //Find vacant Inode in IBitmap
    uint8_t ibitmap[Blk_size];
    lseek(fd, Inode_bitmap_block * Blk_size, SEEK_SET);
    read(fd, ibitmap, Blk_size);

    int inum=-1;
    for (int i = 1; i < Blk_size * 8; i++) { 
        if (!(ibitmap[i/8] & (1 << (i%8)))) {
            inum = i;
            ibitmap[i/8] |= (1 << (i%8));
            break;
        }
    }
    if (inum == -1) {
        printf("No inodes could be found free\n"); 
        close(fd); 
        return 1; 
    }

    //Directory Update pre steps
    struct dirent dir_block[Blk_size / sizeof(struct dirent)];
    lseek(fd, Root_directory_block * Blk_size, SEEK_SET);
    read(fd, dir_block, Blk_size);

    int slot = -1;
    for (int i = 2; i < 128; i++) { // inconsistency here // . and ..
        if (dir_block[i].inode == 0) {
            slot = i;
            break;
        }
    }
    if (slot == -1) { 
        printf("Directory is full\n"); 
        close(fd); 
        return 1; 
    }

    dir_block[slot].inode = inum;
    strncpy(dir_block[slot].name, filename, 27);

    //Inode Table Update pre step(new inode,root size)
    int inodes_per_block = Blk_size / sizeof(struct inode); 
    int itable_blk_idx = Inode_table_starting + (inum / inodes_per_block);
    struct inode itable[inodes_per_block];
    lseek(fd, itable_blk_idx * Blk_size, SEEK_SET);
    read(fd, itable, Blk_size);

    //making new file's iode
    int idx = inum % inodes_per_block;
    memset(&itable[idx], 0, sizeof(struct inode));
    itable[idx].type = 1;  // File
    itable[idx].links = 1;
    itable[idx].size = 0;

    if (itable_blk_idx == Inode_table_starting) {
        itable[0].size = (slot + 1) * sizeof(struct dirent);
    } else {
        //inode in b2,go to b1 and update root
        struct inode root_inodeblock[inodes_per_block];
        lseek(fd, Inode_table_starting * Blk_size, SEEK_SET);
        read(fd, root_inodeblock, Blk_size);
        root_inodeblock[0].size = (slot + 1) * sizeof(struct dirent);
        
        //root block update record
        struct data_record dir_root_record;
        dir_root_record.hdr.type = REC_DATA;
        dir_root_record.hdr.size = sizeof(struct data_record);
        dir_root_record.block_no = Inode_table_starting;
        memcpy(dir_root_record.data, root_inodeblock, Blk_size);
        append_record(fd, &dir_root_record, sizeof(dir_root_record));
    }

    //ppend transactions to journal
    struct data_record dr;
    dr.hdr.type = REC_DATA;
    dr.hdr.size = sizeof(struct data_record);

    //Bitmap logging
    dr.block_no = Inode_bitmap_block;
    memcpy(dr.data, ibitmap, Blk_size);
    append_record(fd, &dr, sizeof(dr));

    //Inode table block logging
    dr.block_no = itable_blk_idx;
    memcpy(dr.data, itable, Blk_size);
    append_record(fd, &dr, sizeof(dr));

    //Directory block logging
    dr.block_no = Root_directory_block;
    memcpy(dr.data, dir_block, Blk_size);
    append_record(fd, &dr, sizeof(dr));

    //commit
    struct rec_header commit = {REC_COMMIT, sizeof(struct rec_header)};
    append_record(fd, &commit, sizeof(commit));

    printf("Metadata for '%s' committed to journal (Inode number %d).\n",filename,inum);
    close(fd);
    return 0;
}

int install() {
    int fd = open("vsfs.img", O_RDWR);
    if (fd < 0) return 1;

    struct journal_header jh;
    lseek(fd, Journal_Block * Blk_size, SEEK_SET);
    read(fd, &jh, sizeof(jh));

    if (jh.magic != JOURNAL_MAGIC) { 
        printf("Invalid journal!\n"); 
        return 1; 
    }

    uint32_t offset=sizeof(struct journal_header);
    
    //appending all record to buffer till comit is found
    struct { uint32_t blk_no; uint8_t data[Blk_size]; } txn[64];
    int trans_count=0;

    while (offset < jh.nbytes_used) {
        struct rec_header rh;
        lseek(fd, (Journal_Block * Blk_size) + offset, SEEK_SET);
        if (read(fd, &rh, sizeof(rh)) != sizeof(rh)) break;

        if (rh.type == REC_DATA) {
            struct data_record dr;
            lseek(fd, (Journal_Block * Blk_size) + offset, SEEK_SET);
            read(fd, &dr, sizeof(dr));
            
            txn[trans_count].blk_no = dr.block_no;
            memcpy(txn[trans_count].data, dr.data, Blk_size);
            trans_count++;
            offset += rh.size;
        } 
        else if (rh.type == REC_COMMIT) {
            //writing in disk
            for (int i = 0; i < trans_count; i++) {
                lseek(fd, txn[i].blk_no * Blk_size, SEEK_SET);
                write(fd, txn[i].data, Blk_size);
            }
            trans_count = 0;
            offset += rh.size;
        } else {
            break; //record is incomplete
        }
    }

    //Checkpoint: Reset journal
    jh.nbytes_used = sizeof(struct journal_header);
    lseek(fd, Journal_Block * Blk_size, SEEK_SET);
    write(fd, &jh, sizeof(jh));

    printf("Journal record installed\n");
    close(fd);
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc<2) {
        printf("Invalid input\n");
        return 1;
    }
    if (strcmp(argv[1],"create")==0 && argc==3) {
        return create(argv[2]);
    } 
    else if (strcmp(argv[1],"install") == 0) {
        return install();
    } 
    else {
        printf("Invalid input.\n");
        return 1;
    }
}