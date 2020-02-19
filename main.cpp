#include <iostream>
#include "rangeallocator.h"

#define TEST(msg)            std::cout << "[line " << __LINE__ << "] " << msg << ": ";
#define CHECK(expr)          std::cout << (!(expr) ? "FAILED" : "OK") << std::endl;

int main(int, char*[])
{
    ralloc_t ra = 0;
    vaddr_t mem = 0;

    const vaddr_t base        = 0x1000;
    const size_t  length      = 4096;
    const size_t  granularity = 64;
    const vaddr_t invalid     = (vaddr_t)-1;
    const vaddr_t hint        = base + length / 2;

    ra = create_range_allocator(base, length, granularity);
    vaddr_t addr = allocate_range(ra, length, ALLOCATE_ANY, 0);


    // Range allocator creation
    TEST("Create at null base address must fail");
    ra = create_range_allocator(0, length, granularity);
    CHECK(ra == 0);

    TEST("Create with null length must fail");
    ra = create_range_allocator(base, 0, granularity);
    CHECK(ra == 0);
        
    TEST("Create with null granularity must fail");
    ra = create_range_allocator(base, length, 0);
    CHECK(ra == 0);

    TEST("Create with granularity greater than length must fail");
    ra = create_range_allocator(base, length, 2*length);
    CHECK(ra == 0);

    TEST("Create with valid parameters should succeed");
    ra = create_range_allocator(base, length, granularity);
    CHECK(ra);


    // ALLOCATE_ANY
    TEST("Trying to ALLOCATE_ANY with null length must fail");
    mem = allocate_range(ra, 0, ALLOCATE_ANY, 0);
    CHECK(mem == invalid);

    TEST("Trying to ALLOCATE_ANY too much memory must fail");
    mem = allocate_range(ra, length + 1, ALLOCATE_ANY, 0);
    CHECK(mem == invalid);

    TEST("Should be able to ALLOCATE_ANY each memory block one at a time");
    bool failed = false;
    for (size_t i = 1; !failed && i <= length / granularity; i++) {
        mem = allocate_range(ra, (i % granularity) || granularity, ALLOCATE_ANY, 0);
        failed = (mem == invalid);
    }
    CHECK(!failed);

    TEST("Trying to ALLOCATE_ANY when all blocks are used must fail");
    mem = allocate_range(ra, granularity, ALLOCATE_ANY, 0);
    CHECK(mem == invalid);

    free_range(ra, base, length);

    TEST("Should be able to ALLOCATE_ANY the full memory");
    mem = allocate_range(ra, length, ALLOCATE_ANY, 0);
    CHECK(mem == base);
    
    free_range(ra, base, length);


    // ALLOCATE_EXACT
    TEST("Trying to ALLOCATE_EXACT with null length must fail");
    mem = allocate_range(ra, 0, ALLOCATE_EXACT, base + length/2);
    CHECK(mem == invalid);

    TEST("Trying to ALLOCATE_EXACT too much memory must fail");                             // |----------------'-------------|   
    mem = allocate_range(ra, length, ALLOCATE_EXACT, hint);                                 //                  ^^^^^^^^^^^^^^^^^^
    CHECK(mem == invalid);

    TEST("ALLOCATE_EXACT should return an address equal to the hint value");
    mem = allocate_range(ra, granularity, ALLOCATE_EXACT, hint);                            // |----------------'-------------|
    CHECK(mem == hint);                                                                     //                  ^              

    TEST("ALLOCATE_EXACT should return an address equal to the hint value");
    mem = allocate_range(ra, granularity, ALLOCATE_EXACT, hint + granularity);              // |----------------_'------------|
    CHECK(mem == hint + granularity);                                                       //                   ^             

    TEST("ALLOCATE_EXACT should return an address equal to the hint value");
    mem = allocate_range(ra, granularity, ALLOCATE_EXACT, hint - granularity);              // |---------------'__------------|
    CHECK(mem == hint - granularity);                                                       //                 ^               

    TEST("Trying to ALLOCATE_EXACT with overlap must fail");
    mem = allocate_range(ra, 4*granularity, ALLOCATE_EXACT, hint - 2*granularity);          // |--------------'___------------|
    CHECK(mem == invalid);                                                                  //                ^^^^             

    free_range(ra, hint - granularity, 3 * granularity);                                    // |---------------^^^------------|

    TEST("Should be able to ALLOCATE_EXACT a block just freed");
    mem = allocate_range(ra, 3*granularity, ALLOCATE_EXACT, hint - granularity);            // |---------------'--------------|
    CHECK(mem == hint - granularity);                                                       //                 ^^^             

    free_range(ra, hint - granularity, 3 * granularity);                                    // |---------------^^^------------|





    // ALLOCATE_ABOVE
    TEST("Trying to ALLOCATE_ABOVE with null length must fail");
    mem = allocate_range(ra, 0, ALLOCATE_ABOVE, hint);
    CHECK(mem == invalid);

    TEST("Trying to ALLOCATE_ABOVE too much memory must fail");
    mem = allocate_range(ra, length, ALLOCATE_ABOVE, hint);                                 // |----------------'-------------|   
    CHECK(mem == invalid);                                                                  //                  ^^^^^^^^^^^^^^^^^^

    TEST("ALLOCATE_ABOVE should return an address greater than or equal to the hint value");
    mem = allocate_range(ra, granularity, ALLOCATE_ABOVE, hint);                            // |----------------'-------------|
    CHECK(mem >= hint);                                                                     //                      ^          

    free_range(ra, mem, granularity);                                                       // |--------------------^---------|

    TEST("Trying to ALLOCATE_ABOVE with not enough blocks must fail");
    mem = allocate_range(ra, length / 4, ALLOCATE_EXACT, hint);                             // |--------------'---------------|          
                                                                                            //                ^^^^^^^^^                  
    mem = allocate_range(ra, length / 2, ALLOCATE_ABOVE, hint - granularity);               // |-------------'_________-------|          
    CHECK(mem == invalid);                                                                  //                         ^^^^^^^^^^^^^^^^  

    free_range(ra, hint, length / 4);                                                       // |--------------^^^^^^^^^-------|



    // ALLOCATE_BELOW
    TEST("Trying to ALLOCATE_BELOW with null length must fail");
    mem = allocate_range(ra, 0, ALLOCATE_BELOW, hint);
    CHECK(mem == invalid);

    TEST("Trying to ALLOCATE_BELOW too much memory must fail");
    mem = allocate_range(ra, length, ALLOCATE_BELOW, hint);                                 // |----------------'-------------|
    CHECK(mem == invalid);                                                                  // ^^^^^^^^^^^^^^^^^^^^^^^         

    TEST("ALLOCATE_BELOW should return an address smaller than or equal to the hint value minus the (aligned) length");
    mem = allocate_range(ra, 4 * granularity, ALLOCATE_BELOW, hint);                        // |----------------'-------------|
    CHECK(mem + 4 * granularity <= hint);                                                   //          ^^^^                   

    free_range(ra, mem, 4 * granularity);                                                   // |--------^^^^------------------|

    TEST("Trying to ALLOCATE_BELOW with not enough blocks must fail");
    mem = allocate_range(ra, length / 4, ALLOCATE_EXACT, hint - length / 4);                // |--------------'---------------|
                                                                                            //         ^^^^^^^^                
    mem = allocate_range(ra, length / 2, ALLOCATE_BELOW, hint);                             // |-------_______'---------------|
    CHECK(mem == invalid);                                                                  // ^^^^^^^^^^^^^^^^  

    free_range(ra, hint - length / 4, length / 4);                                          // |--------^^^^^^^---------------|



    destroy_range_allocator(ra);
}