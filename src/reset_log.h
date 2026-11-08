#pragma once

#include <stddef.h>
#include <stdint.h>

/** @brief Read the reset cause and stored records. Call once from setup(). */
void resetLogBegin();

/** @brief Write pending resets to flash once the boot is stable. Call from loop(). */
void resetLogLoop();

/** @brief Track the lowest largest-free-block value seen before TLS requests. */
void resetLogNoteBlock(uint32_t block);

/**
 * @brief Build the status bar indicator text.
 * @return true if there is something unusual to show.
 */
bool resetLogIndicator(char* line1, size_t line1Size, char* line2, size_t line2Size);
