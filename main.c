#include "stdbool.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "sys/types.h"

#define PAGE_SIZE 256 // 2^8 байт
#define PAGE_TABLE_SIZE 256 //
#define FRAME_TABLE_SIZE 128
#define TLB_SIZE 16

// Структура таблицы страниц
struct page_table_entry
{
    int frame_number;
} page_table[PAGE_TABLE_SIZE];

// Структура виртуального адресного пространства (физической памяти)
struct frame_table_entry
{
    struct frame_entry
    {
        char page[PAGE_SIZE];
        int usage_time; // Целое число, характеризующее последнее время обращения к записи (чем оно больше, тем позднее время)
    } frame;
} physical_memory[FRAME_TABLE_SIZE];

// Структура TLB
struct TLB_entry
{
    int page_number;
    int frame_number;
    int usage_time;
} TLB[TLB_SIZE];

#define PAGE_NUMBER_MASK 0xFFFFFF00 // ~ 2^32 = 4294967040 = 1111111111111111 11111111 00000000 (номер страницы занимает в логическом адресе средние 8 бит)
#define OFFSET_MASK 0x000000FF // 2^8 - 1 = 255 = 0000000000000000 00000000 11111111 (смещение занимает в логическом адресе крайние правые 8 бит)

int main(int argc, char *argv[])
{
    FILE *addresses = fopen(argv[1], "r");
    FILE *backup_page_storage;

    char *logical_address, backup_page[PAGE_SIZE];
    size_t size = 0;
    ssize_t logical_address_length;

    // frame number = [0...127]
    int physical_address, frame_number = 0, page_number, offset, page_error_count = 0;

    int TLB_hit_count = 0, TLB_free_space = TLB_SIZE, TLB_last_added_page_index = 0;
    bool TLB_hit, TLB_miss, out_of_physical_memory = false;

    signed char value;

    for (int i = 0; i < PAGE_TABLE_SIZE; ++i)
    {
        page_table[i].frame_number = -1;
    }

    while ((logical_address_length = getline(&logical_address, &size, addresses)) != -1)
    {
        TLB_hit = false;
        TLB_miss = false;

        if (logical_address[logical_address_length - 1] == '\n')
        {
            logical_address[logical_address_length - 1] = '\0';
        }

        page_number = (atoi(logical_address) & PAGE_NUMBER_MASK) >> 8; // >> 8 - смещение на 8 бит вправо для получения числа без обнулённых цифр с правого края
        offset = atoi(logical_address) & OFFSET_MASK; // & - операция побитового И

        // Обращение к TLB (Translation Lookaside Buffer - Буфер ассоциативной трансляции)
        for (int i = 0; i < TLB_SIZE; ++i)
        {
            TLB[i].usage_time += 1;

            if (!TLB_hit)
            {
                if (TLB[i].page_number == page_number)
                {
                    TLB_hit = true;
                    TLB_miss = false;

                    TLB[i].usage_time = 0; // 0 - к записи только что произошло обращение
                    physical_memory[TLB[i].frame_number].frame.usage_time = 0;

                    physical_address = TLB[i].frame_number << 8 | offset; // << 8 для получения шестнадцатибитного адреса
                    value = physical_memory[TLB[i].frame_number].frame.page[offset];

                    ++TLB_hit_count;
                }
                else
                {
                    TLB_miss = true;
                    TLB_hit = false;
                }
            }
        }

        if (TLB_miss)
        {
            if (page_table[page_number].frame_number != -1)
            {
                physical_address = page_table[page_number].frame_number << 8 | offset;
            }
            else
            {
                ++page_error_count;

                backup_page_storage = fopen("BACKING_STORE.bin", "r");
                int backup_page_number = 0;

                // Считываем из резервного хранилища страниц одну 256 байтную страницу
                while (fread(backup_page, PAGE_SIZE, 1, backup_page_storage))
                {
                    // Если номер считанной страницы совпал с номером страницы, выдавшей ошибку
                    if (backup_page_number == page_number)
                    {
                        // Если все 128 фреймов физической памяти заполнены страницами (отсутствует свободная физическая память)
                        if (frame_number > 127 || out_of_physical_memory)
                        {
                            out_of_physical_memory = true;

                            int maximum_usage_time = 0;
                            int LRU_frame_index = 0;

                            // Ищем самый неиспользуемый (LRU - Least Recently Used) фрейм
                            for (int i = 0; i < FRAME_TABLE_SIZE; ++i)
                            {
                                if (physical_memory[i].frame.usage_time > maximum_usage_time)
                                {
                                    maximum_usage_time = physical_memory[i].frame.usage_time;
                                    LRU_frame_index = i;
                                }
                            }

                            // Перезаписываем его новыми данными
                            memcpy(physical_memory[LRU_frame_index].frame.page, backup_page, PAGE_SIZE);
                            physical_memory[LRU_frame_index].frame.usage_time = 0;

                            frame_number = LRU_frame_index;
                        }
                        else
                        {
                            // Сохраняем страницу из хранилища, а также последнее время обращения к ней в свободном фрейме физической памяти
                            memcpy(physical_memory[frame_number].frame.page, backup_page, PAGE_SIZE);
                            physical_memory[frame_number].frame.usage_time = 0;
                        }

                        // Обновление таблицы страниц
                        page_table[page_number].frame_number = frame_number;

                        // Обновление TLB
                        if (TLB_free_space != 0) // Если в TLB есть свободное место
                        {
                            --TLB_free_space;

                            // Последовательно добавляем в него записи
                            TLB[TLB_last_added_page_index].page_number = page_number;
                            TLB[TLB_last_added_page_index].frame_number = frame_number;
                            TLB[TLB_last_added_page_index].usage_time = 0;

                            ++TLB_last_added_page_index;
                        }
                        else
                        {
                            int maximum_usage_time = 0;
                            int LRU_page_index = 0;

                            // Если в TLB нет свободного места, то ищем в нём LRU-запись
                            for (int i = 0; i < TLB_SIZE; ++i)
                            {
                                if (TLB[i].usage_time > maximum_usage_time)
                                {
                                    maximum_usage_time = TLB[i].usage_time;
                                    LRU_page_index = i;
                                }
                            }

                            // Перезаписываем её новыми данными
                            TLB[LRU_page_index].page_number = page_number;
                            TLB[LRU_page_index].frame_number = frame_number;
                            TLB[LRU_page_index].usage_time = 0;
                        }

                        physical_address = frame_number << 8 | offset;

                        if (!out_of_physical_memory)
                        {
                            ++frame_number;
                        }

                        break;
                    }
                    else
                    {
                        ++backup_page_number;
                    }
                }
            }

            value = physical_memory[page_table[page_number].frame_number].frame.page[offset];
            physical_memory[page_table[page_number].frame_number].frame.usage_time = 0;
        }
        
        printf("Virtual address: %s Physical address: %d Value: %d\n", logical_address, physical_address, value);

        for (int i = 0; i < FRAME_TABLE_SIZE; ++i)
        {
            physical_memory[i].frame.usage_time += 1;
        }
    }

    printf("\nPage error rate = %.f%%\n", page_error_count / 1000.0 * 100);
    printf("TLB hit rate = %.f%%\n", TLB_hit_count / 1000.0 * 100);

    fclose(addresses);
    fclose(backup_page_storage);

    return 0;
}
