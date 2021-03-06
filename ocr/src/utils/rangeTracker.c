/**
 * @brief Utilities to track the tagging of non overlapping ranges
 *
 */

/*
 * This file is subject to the license agreement located in the file LICENSE
 * and cannot be distributed without it. This notice cannot be
 * removed or modified.
 */

#include "ocr-hal.h"
#include "debug.h"
#include "ocr-types.h"
#include "utils/ocr-utils.h"
#include "utils/rangeTracker.h"

#define DEBUG_TYPE UTIL

// Defines to make this easier to port to
// other platforms if needed
#define INIT_MALLOC(area, size)  chunkInit((area), (size))
#define MALLOC(area, size) chunkMalloc((area), (u64)(size))
#define FREE(area, addr)   chunkFree((area), (void*)(addr))
#define INIT_LOCK(addr) do {*addr = 0;} while(0);
#define LOCK(addr) do {hal_lock32(addr);} while(0);
#define UNLOCK(addr) do {hal_unlock32(addr);} while(0);

// Very stupid allocator that just hands out chunks of things
void chunkInit(u64 startChunk, u64 size) {
    u64* bitVector = (u64*)startChunk;
    *bitVector = 0x0ULL;
    ASSERT(size >= sizeof(u64));
    ASSERT(size <= sizeof(u64)+64*sizeof(avlBinaryNode_t));
    size -= sizeof(u64);
    ASSERT(size % sizeof(avlBinaryNode_t) == 0); // Let's be clean
    size /= sizeof(avlBinaryNode_t);
    // Size now contains the number of "slots" we need to have
    u64 shiftAmount = 0;
    if(size >= 64) {
        *bitVector |= 0xFFFFFFFFFFFFFFFFULL;
        size -= 64;
        shiftAmount += 64;
    }
    if(size >= 32) {
        *bitVector |= (0xFFFFFFFFULL)<<(shiftAmount);
        size -= 32;
        shiftAmount += 32;
    }
    if(size >= 16) {
        *bitVector |= (0xFFFFULL)<<(shiftAmount);
        size -= 16;
        shiftAmount += 16;
    }
    if(size >= 8) {
        *bitVector |= (0xFFULL)<<(shiftAmount);
        size -= 8;
        shiftAmount += 8;
    }
    if(size >= 4) {
        *bitVector |= (0xFULL)<<(shiftAmount);
        size -= 4;
        shiftAmount += 4;
    }
    if(size >= 2) {
        *bitVector |= (0x3ULL)<<(shiftAmount);
        size -= 2;
        shiftAmount += 2;
    }
    if(size >= 1) {
        *bitVector |= (0x1ULL)<(shiftAmount);
        size -= 1;
        shiftAmount += 1;
    }
    ASSERT(size == 0);
}

void* chunkMalloc(u64 startChunk, u64 size) {
    u64* bitVector = (u64*)startChunk;
    ASSERT(size <= sizeof(avlBinaryNode_t));
    if(*bitVector == 0) {
        return NULL;;
    } else {
        u64 bitId = fls64(*bitVector);
        *bitVector &= ~(1ULL<<bitId);
        return (void*)(startChunk + sizeof(u64) + bitId*sizeof(avlBinaryNode_t));
    }
}

void chunkFree(u64 startChunk, void* addr) {
    u64* bitVector = (u64*)startChunk;
    u64 pos = (u64)addr;
    pos = (u64)addr - startChunk - sizeof(u64);
    ASSERT(pos % sizeof(avlBinaryNode_t) == 0);
    pos /= sizeof(avlBinaryNode_t);
    *bitVector |= (1ULL<<pos);
}

// AVL functions
/**
 * @brief Insert into the AVL tree rooted at root
 *
 * This function tries to insert (key, value) into the tree. If the
 * key already exists, it will not be re-inserted but the
 * value will be updated
 *
 * @param startChunk[in]        Start of area (for free/malloc)
 * @param root[in]              Root of the tree to insert into (or NULL)
 * @param key[in]               Key to insert
 * @param value[in]             Value to insert
 * @param node[out]             Returns the pointer to the inserted node
 * @return Root of the new tree
 */
static avlBinaryNode_t* avlInsert(u64 startChunk, avlBinaryNode_t *root,
                                  u64 key, u64 value, avlBinaryNode_t **node);

/**
 * @brief Search for a key in the binary tree
 *
 * The mode value determines the type of search (exact, lower-bound
 * or upper-bound):
 *     - exact will return the node that contains the key exactly
 *     - lower-bound will return the node that contains the
 *       largest key that is less-than the input key
 *     - exact-lower-bound will return either an exact match
 *       or the lower-bound
 *     - upper-bound will return the node that contains the
 *       smallest key that is larger-than the input key
 *     - exact-upper-bound will return either an exact match
 *       or the upper bound
 * @param root[in]              Root fo the tree to insert into
 * @param key[in]               Key to search for
 * @param mode[in]              0=exact,
 *                              -1=exact-lower-bound
 *                              -2=lower-bound
 *                               1=exact-upper-bound
 *                               2=upper-bound
 * @return NULL if no match found or the node
 */
static avlBinaryNode_t* avlSearch(avlBinaryNode_t *root, u64 key, s8 mode);

/**
 * @brief Deletes a value from the tree
 *
 * @param root[in]              Root fo the tree from which to remove
 * @param key[in]               Key to remove
 * @param modifiedNode[out]     Will return the pointer of the node that
 *                              was modified (if any). This is the
 *                              node that should have been deleted but was
 *                              instead replaced by another one
 * @param deletedNode[out]      Will return the pointer to the node that
 *                              was removed from the tree. It has not
 *                              been freed. This node will either have
 *                              the key 'key' or a different key (the one
 *                              of modifiedNode)
 *
 * @return The root of the new tree
 */
static avlBinaryNode_t* avlDelete(avlBinaryNode_t *root, u64 key,
                                  avlBinaryNode_t **modifiedNode,
                                  avlBinaryNode_t **deletedNode);

/**
 * @brief Destroys the tree and frees everything
 *
 * @param[in] startChunk        Start of the space (for alloc/free)
 * @param[in] root              Root of the tree to destroy
 */
static void avlDestroy(u64 startChunk, avlBinaryNode_t *root);

/********************************************
 * HELPER FUNCTIONS FOR AVL TREES           *
 ********************************************/

static u32 height(avlBinaryNode_t *node) {
    if(node) return node->height;
    return 0; // All nodes will have height at least one usually
}

static avlBinaryNode_t *newTree(u64 startChunk) {
    avlBinaryNode_t *tree = (avlBinaryNode_t*)MALLOC(startChunk, sizeof(avlBinaryNode_t));
    DPRINTF(DEBUG_LVL_INFO, "Created AVL tree/node @ 0x%"PRIx64"\n", (u64)tree);
    ASSERT(tree);
    tree->key = 0;
    tree->value = 0;
    tree->left = tree->right = NULL;
    tree->height = 1; // 0 is invalid height

    return tree;
}

// Rotate the root with its left child.
// Returns the new root
static avlBinaryNode_t* rotateWithLeft(avlBinaryNode_t *root) {
    avlBinaryNode_t *leftChild = root->left;

    root->left = leftChild->right;
    // Update height
    root->height = (height(root->left) > height(root->right)?height(root->left):
                    height(root->right)) + 1;

    leftChild->right = root;
    leftChild->height = (root->height > height(leftChild->left)?root->height:
                         height(leftChild->left)) + 1;

    return leftChild;
}

// Rotate the root with its right child
// Returns the new root
static avlBinaryNode_t* rotateWithRight(avlBinaryNode_t *root) {
    avlBinaryNode_t *rightChild = root->right;

    root->right = rightChild->left;
    root->height = (height(root->left) > height(root->right)?height(root->left):
                    height(root->right)) + 1;

    rightChild->left = root;
    rightChild->height = (root->height > height(rightChild->right)?root->height:
                          height(rightChild->right)) + 1;

    return rightChild;
}

// Find the minimum in a tree
static avlBinaryNode_t* avlFindMin(avlBinaryNode_t *root) {
    DPRINTF(DEBUG_LVL_VVERB, "Looking for minimum in tree rooted at 0x%"PRIx64"\n",
            (u64)root);

    avlBinaryNode_t *parent = NULL;
    while(root) {
        parent = root;
        root = root->left;
    }
    DPRINTF(DEBUG_LVL_VVERB, "Tree @ 0x%"PRIx64": Found minimum node 0x%"PRIx64" (key: 0x%"PRIx64")\n",
            (u64)root, (u64)parent, (u64)(parent?parent->key:0));
    return parent;
}

// Find the maximum in a tree
static avlBinaryNode_t* avlFindMax(avlBinaryNode_t *root) {
    DPRINTF(DEBUG_LVL_VVERB, "Looking for maximum in tree rooted at 0x%"PRIx64"\n",
            (u64)root);

    avlBinaryNode_t *parent = NULL;
    while(root) {
        parent = root;
        root = root->right;
    }
    DPRINTF(DEBUG_LVL_VVERB, "Tree @ 0x%"PRIx64": Found maximum node 0x%"PRIx64" (key: 0x%"PRIx64")\n",
            (u64)root, (u64)parent, (u64)(parent?parent->key:0));
    return parent;
}

// Helper function for the search
static avlBinaryNode_t* avlSearchSub(avlBinaryNode_t *root, avlBinaryNode_t *upperBoundParent, u64 key, s8 mode) {
    ASSERT(root);
    DPRINTF(DEBUG_LVL_VERB, "Going to search for 0x%"PRIx64" (mode %"PRId32") in root 0x%"PRIx64"\n",
            key, mode, (u64)root);
    if(key == root->key) {
        // Ah-ha, we found the node
        DPRINTF(DEBUG_LVL_VVERB, "Found node for 0x%"PRIx64" @ 0x%"PRIx64"\n",
                key, (u64)root);
        switch(mode) {
        case 0:
        case -1:
        case 1:
            return root;
        case -2:
            // We need to find the lower-bound so, the biggest element in the left sub-tree
            if(root->left)
                return avlFindMax(root->left);
            return NULL;
        case 2:
            // We need to find the upper-bound so we return the upper-bound parent
            if(root->right)
                return avlFindMin(root->right);
            // If there is no right child, we see if we have a parent
            // that is bigger
            return upperBoundParent;
        default:
            ASSERT(0);
        }
        ASSERT(0); // Unreachable
    }
    if(key < root->key) {
        // Update upperBoundParent if needed
        if(upperBoundParent) {
            if(upperBoundParent->key > root->key)
                upperBoundParent = root;
        } else
            upperBoundParent = root;
        // We need to go search on the left of the tree
        if(root->left) {
            DPRINTF(DEBUG_LVL_VVERB, "Going to search for 0x%"PRIx64" to the left: from 0x%"PRIx64" to 0x%"PRIx64"\n",
                    key, (u64)root, (u64)(root->left));
            return avlSearchSub(root->left, upperBoundParent, key, mode);
        }
        DPRINTF(DEBUG_LVL_VVERB, "Cannot search further for 0x%"PRIx64" (going left)\n", key);
        // Here, there is no left
        switch(mode) {
        case 0:
        case -1:
        case -2:
            return NULL; // Did not find an exact match or a lower-bound
        case 1:
        case 2:
            return upperBoundParent;
        default:
            ASSERT(0);
        }
        ASSERT(0); // Unreachable
    }
    if(key > root->key) {
        // We need to go search on the right of the tree
        if(root->right) {
            DPRINTF(DEBUG_LVL_VVERB, "Going to search for 0x%"PRIx64" to the right: from 0x%"PRIx64" to 0x%"PRIx64"\n",
                    key, (u64)root, (u64)(root->right));
            return avlSearchSub(root->right, upperBoundParent, key, mode);
        }
        DPRINTF(DEBUG_LVL_VVERB, "Cannot search further for 0x%"PRIx64" (going right)\n", key);
        // Here there is no right
        switch(mode) {
        case 0:
            return NULL; // Did not find an exact match
        case -1:
        case -2:
            return root; // This is the lower bound
        case 1:
        case 2:
            return upperBoundParent;
        default:
            ASSERT(0);
        }
        ASSERT(0); // unreachable
    }
    ASSERT(0); // Unreachable
    return NULL; // Keep compiler happy
}

/********************************************
 * HELPER FUNCTIONS FOR RANGES              *
 ********************************************/

// Remove the tag referred to by idx
// Assumes the lock is held (for the range)
static void unlinkTag(rangeTracker_t *range, u64 idx) {
    ASSERT(idx < range->nextTag);
    tagNode_t *tag = &(range->tags[idx]);
    u64 keyToRemove = tag->node->key;
    DPRINTF(DEBUG_LVL_VERB, "Range 0x%"PRIx64": unlinking node IDX %"PRId64" for tag %"PRId32" and key 0x%"PRIx64"\n",
            (u64)range, idx, (u32)tag->tag, keyToRemove);

    // Relink the "linked-list"
    if(tag->nextTag) {
        range->tags[tag->nextTag-1].prevTag = tag->prevTag;
    }
    if(tag->prevTag) {
        range->tags[tag->prevTag-1].nextTag = tag->nextTag;
    } else {
        // This means we were the head
        range->heads[tag->tag].headIdx = tag->nextTag;
    }

    // Swap with the last tag
    ASSERT(range->nextTag > 0); // We are unlinking one so there should be one that exists
    if(idx != range->nextTag - 1) {
        range->tags[idx].node = range->tags[range->nextTag - 1].node;
        range->tags[idx].tag = range->tags[range->nextTag - 1].tag;
        range->tags[idx].prevTag = range->tags[range->nextTag - 1].prevTag;
        range->tags[idx].nextTag = range->tags[range->nextTag - 1].nextTag;
        // Update the node to point to the new IDX
        range->tags[idx].node->value = idx;
    }
    range->nextTag -= 1;

    // Remove the node from the tree
    avlBinaryNode_t *modified = NULL;
    avlBinaryNode_t *deleted = NULL;
    range->rangeSplits = avlDelete(range->rangeSplits, keyToRemove, &modified, &deleted);

    // If we "moved" a node, reflect that
    if(modified) {
        ASSERT(deleted->key != keyToRemove);
        range->tags[modified->value].node = modified;
    } else {
        ASSERT(deleted->key = keyToRemove);
    }
    FREE(range->startBKHeap, deleted);
}

// Remove the tag referred to by idx
// Assumes the lock is held (for the range)
static void linkTag(rangeTracker_t *range, u64 addr, ocrMemoryTag_t tag) {

    ASSERT(tag < MAX_TAG);
    u32 tagIdxToUse = range->nextTag++;
    ASSERT(tagIdxToUse < range->maxSplits);
    avlBinaryNode_t *insertedNode = NULL;
    range->rangeSplits = avlInsert(range->startBKHeap, range->rangeSplits, addr,
                                   tagIdxToUse, &insertedNode);
    ASSERT(insertedNode);
    range->tags[tagIdxToUse].node = insertedNode;
    range->tags[tagIdxToUse].tag = tag;
    range->tags[tagIdxToUse].nextTag = range->heads[tag].headIdx;
    range->tags[tagIdxToUse].prevTag = 0;
    range->heads[tag].headIdx = tagIdxToUse + 1;
    if(range->tags[tagIdxToUse].nextTag) {
        range->tags[range->tags[tagIdxToUse].nextTag].prevTag = tagIdxToUse + 1;
    }
}

/********************************************
 * FUNCTIONS FOR AVL TREES                  *
 ********************************************/
static avlBinaryNode_t* avlInsert(u64 startChunk, avlBinaryNode_t *root,
                                  u64 key, u64 value, avlBinaryNode_t **node) {
    DPRINTF(DEBUG_LVL_VERB, "Inserting (0x%"PRIx64", %"PRId64") into tree @ 0x%"PRIx64"\n",
            key, value, (u64)root);
    if(!root) {
        DPRINTF(DEBUG_LVL_VVERB, "Creating new node\n");
        root = newTree(startChunk);
        root->key = key;
        root->value = value;
        if(node) *node = root;
        return root;
    }
    // Here we already have a tree so we need to figure out where to insert things
    if(key == root->key) {
        DPRINTF(DEBUG_LVL_VVERB, "Replacing value of existing node (from %"PRId64" to %"PRId64")\n",
                root->value, value);
        // Well, we got lucky, the key is in the tree, update value and return
        root->value = value;
        if(node) *node = root;
        return root;
    }

    if(key < root->key) {
        // Go insert to the left
        DPRINTF(DEBUG_LVL_VVERB, "Inserting to the left (from 0x%"PRIx64" to 0x%"PRIx64")\n",
                (u64)root, (u64)root->left);
        root->left = avlInsert(startChunk, root->left, key, value, node);
        DPRINTF(DEBUG_LVL_VVERB, "New left sub-tree is 0x%"PRIx64" (root 0x%"PRIx64")\n",
                (u64)root->left, (u64)root);

        // Check if we need to rebalance, only rebalance if we are not in -1:+1 range
        // We only test for 2 because the height will only increase in root->left
        if(height(root->left) - height(root->right) == 2) {
            // If we inserted to the right of the left node, we need to do two
            // rotates (see https://en.wikipedia.org/wiki/File:AVL_Tree_Rebalancing.svg
            // for the figures)
            if(key > root->left->key) {
                root->left = rotateWithRight(root->left);
            }
            // Fallthrough and single rotate if inserted to the left of the left
            root = rotateWithLeft(root);
        }
    } else {
        ASSERT(key > root->key);
        // Got insert on the right
        DPRINTF(DEBUG_LVL_VVERB, "Inserting to the right (from 0x%"PRIx64" to 0x%"PRIx64")\n",
                (u64)root, (u64)root->right);
        root->right = avlInsert(startChunk, root->right, key, value, node);
        DPRINTF(DEBUG_LVL_VVERB, "New right sub-tree is 0x%"PRIx64" (root 0x%"PRIx64")\n",
                (u64)root->right, (u64)root);

        // Again, check for rebalance
        if(height(root->right) - height(root->left) == 2) {
            // If inserted to the left of the right node, double rebalance
            if(key < root->right->key) {
                root->right = rotateWithLeft(root->right);
            }
            // Fallthrough
            root = rotateWithRight(root);
        }
    }
    // Update height
    root->height = (height(root->right) > height(root->left)?height(root->right):height(root->left)) + 1;

    return root;
}

static avlBinaryNode_t* avlSearch(avlBinaryNode_t *root, u64 key, s8 mode) {
    return avlSearchSub(root, NULL, key, mode);
}

static avlBinaryNode_t* avlDelete(avlBinaryNode_t *root, u64 key,
                                  avlBinaryNode_t **modifiedNode,
                                  avlBinaryNode_t **deletedNode) {
    u64 removedKey = key;
    if(root) {
        if(root->key == key) {
            // Found the node
            if(root->right && root->left) {
                // We have two children, this is the complicated case
                avlBinaryNode_t *tradePlace = avlFindMax(root->left);
                // Replace root's value with the values in tradePlace
                root->key = tradePlace->key;
                root->value = tradePlace->value;
                // Now go off and delete tradePlace. We start at root->left
                // So that the tree is properly rebalanced
                removedKey = root->key;
                *modifiedNode = tradePlace;
                root->left = avlDelete(root->left, root->key, modifiedNode,
                                       deletedNode);
            } else {
                *deletedNode = root;
                return root->right?root->right:root->left;
            }
        } else if(root->key < key) {
            // Search on the right
            root->right = avlDelete(root->right, key,
                                    modifiedNode, deletedNode);
        } else {
            ASSERT(root->key > key);
            root->left = avlDelete(root->left, key,
                                   modifiedNode, deletedNode);
        }
        // If we reach here, we came back from a recursive
        // call to avlDelete so we need to rebalance
        // the tree going up (potentially)
        // Note the comparison. It works for unsigned values
        if(height(root->left) > height(root->right) + 1) {
            // Left child is too large, we need to rebalance
            // Same logic as for insert

            if(removedKey > root->left->key) {
                root->left = rotateWithRight(root->left);
            }
            // Fallthrough and single rotate if removed to the left of the left
            root = rotateWithLeft(root);
        } else if(height(root->right) > height(root->left) + 1) {
            // Right child is too large, we need to rebalance
            if(removedKey < root->right->key) {
                root->right = rotateWithLeft(root->right);
            }
            // Fallthrough
            root = rotateWithRight(root);
        }
    }
    return root;
}

static void avlDestroy(u64 startChunk, avlBinaryNode_t *root) {
    if(root) {
        if(root->left) avlDestroy(startChunk, root->left); // Check just avoids one recursion
        if(root->right) avlDestroy(startChunk, root->right);
        FREE(startChunk, root);
    }
}

// Range functions
rangeTracker_t *initializeRange(u32 maxSplits,
                     u64 minRange, u64 maxRange, ocrMemoryTag_t initTag) {
    ASSERT(minRange < maxRange);
    ASSERT(initTag < MAX_TAG);
    ASSERT(maxSplits > 0);
    u32 i;

    rangeTracker_t *dest = (rangeTracker_t *)minRange;

    LOCK(&(dest->lock));           // pool->lock is already 0 at startup (for x86, it's done at mallocBegin())
    if (dest->startBKHeap) {       // already init'ed? use startBKHeap as a initialization flag
        ASSERT(dest->count);
        DPRINTF(DEBUG_LVL_INFO, "Initializing a range @ 0x%"PRIx64" from 0x%"PRIx64" to 0x%"PRIx64" -- SKIP\n",
            (u64)dest, minRange, maxRange);
        goto init_range_skip;
    }

    dest->count++;                 // initialization count, assuming zeroed at startup
    dest->minimum = minRange;
    dest->startBKHeap = minRange + sizeof(rangeTracker_t) + sizeof(tagNode_t)*maxSplits; // Start of our book-keeping heap
    dest->maximum = maxRange;
    dest->maxSplits = maxSplits;
    dest->nextTag = 1; // We will use tags[0]

    // We use the beginning as our tags table and then we use a stupid
    // allocator for the tree nodes
    dest->tags = (tagNode_t *)(minRange + sizeof(rangeTracker_t));
    INIT_MALLOC(dest->startBKHeap, sizeof(u64) +
                dest->maxSplits*sizeof(avlBinaryNode_t)); // We allocate at most the number of maxsplits

    DPRINTF(DEBUG_LVL_INFO, "Initializing a range @ 0x%"PRIx64" from 0x%"PRIx64" to 0x%"PRIx64" with tag %"PRId32"\n",
            (u64)dest, minRange, maxRange, initTag);

    dest->rangeSplits = NULL;


    for(i = 0; i < MAX_TAG; ++i) {
        dest->heads[i].headIdx = 0;

        //    This is Romain's (disabled) fine-grained locking code for the future.
        //    See rangeTracker.h tagHead_t struct
        //    INIT_LOCK(&(dest->heads[i].lock));
    }

    // Set up one point with initTag

    dest->rangeSplits = avlInsert(dest->startBKHeap, dest->rangeSplits, minRange, 0, NULL);
    ASSERT(dest->rangeSplits);

    dest->tags[0].tag = initTag;
    dest->tags[0].node = dest->rangeSplits;
    dest->tags[0].nextTag = 0;
    dest->tags[0].prevTag = 0;

    dest->heads[initTag].headIdx = 1; // Offset by 1

    // Now say that the first part is reserved for OS stuff (basically our book-keeping space)
    //
    // Brief layout of this reserved parts
    // (1) rangeTracker_t at the starting addres 'minRange'
    // (2) array of tagNode_t,        count=maxSplits
    // (3) bitVector for BKHeap (startBKHeap)
    // (4) array of avlBinaryNode_t , count=maxSplits
    // Thus, the size below.
    splitRange(dest, dest->minimum, sizeof(rangeTracker_t) + sizeof(u64) + dest->maxSplits*(sizeof(tagNode_t) + sizeof(avlBinaryNode_t)),
               RESERVED_TAG, 1);  // this is only call which skipLock == 1

init_range_skip:
    UNLOCK(&(dest->lock));
    return dest;
}

void destroyRange(rangeTracker_t *self) {

    DPRINTF(DEBUG_LVL_INFO, "Destroying range @ 0x%"PRIx64"", (u64)self);
    LOCK(&self->lock);
    avlDestroy(self->startBKHeap, self->rangeSplits);
    UNLOCK(&self->lock);
}

// 'skipLock' is used by initializeRange() function
u8 splitRange(rangeTracker_t *range, u64 startAddr, u64 size, ocrMemoryTag_t tag, u32 skipLock) {

    // BUG #593: Convert some asserts to return error codes
    /*
     * Explanation: to split the range, we are actually going to insert
     * two points S and E at startAddr and startAddr+size. The following
     * diagram illustrates the most general situation
     * s0     S    s1   s2   s3   E       s4
     * |------|----|----|----|----|-------|
     *     t0   t0   t1   t2   t3    t3
     * s0 or s1 and S could be the same and E and s4 or s3
     * could be the same.
     * [s0; s1[ straddles the S insertion point
     * [s1; s2[ and [s2; s3[ are fully contained
     * [s3; s4[ straddle the E insertion point
     * After insertion, we want:
     * s0     S                   E       s4
     * |------|-------------------|-------|
     *     t0           t4           t3
     * so we see that the affected points are:
     *   - s1, s2, s3
     * The s0 and s4 stay the same as their tag
     * values do not change
     */
    avlBinaryNode_t *v;
    u64 lookupKey = startAddr + size + 1;
    u32 oldLastTag = MAX_TAG + 1;
    if (!skipLock)
        LOCK(&(range->lock)); // Enter critical section to search the tree

    DPRINTF(DEBUG_LVL_VERB, "Splitting range 0x%"PRIx64": adding %"PRId32" for [0x%"PRIx64"; 0x%"PRIx64"[\n",
            (u64)range, (u32)tag, startAddr, startAddr + size);
    // Start by removing all old values between start and end (lookupKey)
    do {
        v = avlSearch(range->rangeSplits, lookupKey, -1); // Search lower-bound or exact
        if(v) {
            if(oldLastTag > MAX_TAG) {
                oldLastTag = range->tags[v->value].tag;
                DPRINTF(DEBUG_LVL_VVERB, "Range 0x%"PRIx64": Tag post split will be %"PRId32"\n",
                        (u64)range, oldLastTag);
            }
            if(v->key >= startAddr)
                unlinkTag(range, v->value);
            else
                break;
        }
    } while(range->rangeSplits); // We may remove everything
    ASSERT(oldLastTag < MAX_TAG);

    // Add start and end points
    linkTag(range, startAddr, tag);
    linkTag(range, startAddr + size + 1, oldLastTag);

    if (!skipLock)
        UNLOCK(&(range->lock));
    return 0;
}

u8 getTag(rangeTracker_t *range, u64 addr, u64 *startRange, u64 *endRange, ocrMemoryTag_t *tag) {

    ASSERT(range);
    ASSERT(addr >= range->minimum && addr < range->maximum);

    LOCK(&(range->lock));
    avlBinaryNode_t *lowerBound = avlSearch(range->rangeSplits, addr, -1);
    avlBinaryNode_t *upperBound = avlSearch(range->rangeSplits, addr, 2);

    ASSERT(lowerBound); // This should always be in the tree
    if(startRange)
        *startRange = lowerBound->key;
    *tag = range->tags[lowerBound->value].tag;
    if(endRange)
        *endRange = upperBound?upperBound->key:range->maximum;
    UNLOCK(&(range->lock));
    return 0;
}

u8 getRegionWithTag(rangeTracker_t *range, ocrMemoryTag_t tag, u64 *startRange, u64 *endRange,
                    u64 *iterate) {

    ASSERT(tag < MAX_TAG);
    u64 iterationCount = *iterate;
    if(iterationCount >= range->maxSplits)
        return 3;

    LOCK(&(range->lock));
    if(range->heads[tag].headIdx) {
        tagNode_t *tagNode = &(range->tags[range->heads[(u32)tag].headIdx - 1]);
        while(tagNode->nextTag && iterationCount) {
            tagNode = &(range->tags[tagNode->nextTag - 1]);
            --iterationCount;
        }
        if(iterationCount) {
            UNLOCK(&(range->lock));
            return 1; // Not found or no more things to iterate
        }
        ASSERT(tag);
        *startRange = tagNode->node->key;
        avlBinaryNode_t *upperBound = avlSearch(range->rangeSplits, *startRange, 2);
        *endRange = upperBound?upperBound->key:range->maximum;
        *iterate = iterationCount + 1; // BUG #593: Not good for last range

        UNLOCK(&(range->lock));
        return 0;
    }

    UNLOCK(&(range->lock));
    return 2;
}
