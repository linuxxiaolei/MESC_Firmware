/*
 * flash_driver.c
 *
 *  Created on: Dec 28, 2020
 *      Author: salavat.magazov
 */

#include "flash_driver.h"

#include "stm32fxxx_hal.h"

#ifdef STM32F405xx
static uint32_t const flash_map[] = {
    // 4 x  16k
    FLASH_BASE + (0 * (16 << 10)),
    FLASH_BASE + (1 * (16 << 10)),
    FLASH_BASE + (2 * (16 << 10)),
    FLASH_BASE + (3 * (16 << 10)),
    // 1 x  64k
    FLASH_BASE + (4 * (16 << 10)),
    // 7 x 128k
    FLASH_BASE + (4 * (16 << 10)) + (64 << 10) + (0 * (128 << 10)),
    FLASH_BASE + (4 * (16 << 10)) + (64 << 10) + (1 * (128 << 10)),
    FLASH_BASE + (4 * (16 << 10)) + (64 << 10) + (2 * (128 << 10)),
    FLASH_BASE + (4 * (16 << 10)) + (64 << 10) + (3 * (128 << 10)),
    FLASH_BASE + (4 * (16 << 10)) + (64 << 10) + (4 * (128 << 10)),
    FLASH_BASE + (4 * (16 << 10)) + (64 << 10) + (5 * (128 << 10)),
    FLASH_BASE + (4 * (16 << 10)) + (64 << 10) + (6 * (128 << 10)),
    // END
    FLASH_BASE + (4 * (16 << 10)) + (64 << 10) + (7 * (128 << 10)),
};

static uint32_t getFlashSectorAddress(uint32_t const sector) {
  return flash_map[sector];
}

static uint32_t getFlashSectorIndex(uint32_t const address) {
  for (uint32_t i = 0; i < ((sizeof(flash_map) / sizeof(*flash_map)) - 1);
       i++) {
    if ((flash_map[i] <= address) && (address < flash_map[i + 1])) {
      return i;
    }
  }

  // error
  return UINT32_MAX;
}
#endif

#ifdef STM32F303xC
uint32_t *const p_flash =
    (uint32_t *)(0x0801F800);  // (see STM32Fxxx_FLASH.ld)
                               // FLASH_BASE+size-FLASH_PAGE_SIZE [303]
// FLASH_BASE+getFlashSectorOffset(FLASH_SECTOR_TOTAL-1) [405]
// OR
// FLASH_END-getFlashSectorSize(FLASH_SECTOR_TOTAL-1)
/*
12 sectors
4 x  16k
1 x  64k
7 x 128k
*/
#endif

static uint32_t eraseFlash();

uint32_t writeFlash(uint32_t const *const p_data, uint32_t const count) {
  uint32_t number_written = 0;
  uint32_t const *p_data_runner = p_data;
  HAL_FLASH_Unlock();  // fixme: check unlocking is successful before
                       // proceeding.
  uint32_t *p_flash = getFlashAddress();
  /* if intended destination is not empty... */
  if (*p_flash != EMPTY_SLOT) {
    /* ...erase entire page before proceeding. */
    uint32_t result = eraseFlash();
    if (result != EMPTY_SLOT) {
      return (number_written);
    }
  }
  /* write all p_data in 32-bit words into flash. */
  uint32_t const *p_flash_runner = p_flash;
  for (int i = 0; i < count; i++, p_flash_runner++, p_data_runner++) {
    if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, (uint32_t)p_flash_runner,
                          *p_data_runner) == HAL_OK) {
      number_written++;
    } else {
      break;
    }
  }
  HAL_FLASH_Lock();
  return (number_written);
}

uint32_t readFlash(uint32_t *const p_data, uint32_t const count) {
  uint32_t number_read = 0;
  uint32_t *p_data_runner = p_data;
  uint32_t *p_flash_runner = getFlashAddress();
  for (int i = 0; i < count; i++, p_data_runner++, p_flash_runner++) {
    *p_data_runner = *p_flash_runner;
    if (*p_flash_runner != EMPTY_SLOT) {
      number_read++;
    }
  }
  return (number_read);
}

/**
 * Erase single page of data.
 * @return Erase status. 0xFFFFFFFF means successful erase.
 * Note: this is not a public funcion. It must be used inside this code only as
 * it assumes flash is unlocked and it does not lock it after erase. Dangerous
 * if misused.
 */
static uint32_t eraseFlash() {
  FLASH_EraseInitTypeDef page_erase;
#ifdef STM32F303xC
  page_erase.TypeErase = FLASH_TYPEERASE_PAGES;  // FLASH_TYPEERASE_SECTORS
  page_erase.PageAddress = (uint32_t)p_flash;    // Banks & Sector
  page_erase.NbPages = 1;                        // NbSectors
#endif
#ifdef STM32F405xx
  page_erase.TypeErase = FLASH_TYPEERASE_SECTORS;
  page_erase.Banks = FLASH_BANK_1;  // (This should be ignored)
  page_erase.Sector = getFlashSectorIndex(getFlashAddress());  // aka "11"
  page_erase.NbSectors = 1;
#endif
  uint32_t result = 0;
  HAL_FLASHEx_Erase(&page_erase, &result);
  return (result);
}

uint32_t *const getFlashAddress() {
#ifdef STM32F303xC
  return (p_flash);
#endif
#ifdef STM32F405xx
  return getFlashSectorAddress(11);
  // TODO const "11"
#endif
}