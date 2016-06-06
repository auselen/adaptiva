#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// An adaptive integer array where we pack as many
// integers in 32bit as feasible.

#define BITS_IN_INT ((int)(sizeof(int) * 8))
#define VALS_IN_INT(ent) (BITS_IN_INT / ent)

static int entropy;     // used bits per value
static unsigned int *buffer;
static int size;        // number of elements
static int allocated;
static int signed_values;

static void init() {
    entropy = 1;
    size = BITS_IN_INT;
    signed_values = 0;
    buffer = malloc(sizeof(int));
	allocated = sizeof(int);
}

static void deinit() {
    free(buffer);
    buffer = NULL;
}

static inline int _get(unsigned int *buf, size_t index, int entropy) {
	int j = index / VALS_IN_INT(entropy);
	int off = index % VALS_IN_INT(entropy);
	int mask = (1 << entropy) - 1;
	int v = buf[j];

	v >>= off * entropy;
	v &= mask;
	int sign_mask = signed_values << (entropy - 1);
	v = (v ^ sign_mask) - sign_mask;

	return v;
}

static inline void _put(unsigned int *buf, size_t index, int value, int entropy) {	
	int j = index / VALS_IN_INT(entropy);
	int off = index % VALS_IN_INT(entropy);
	unsigned int mask = (1 << entropy) - 1;
	unsigned int v = buf[j];
	v &= ~(mask << (entropy * off));
	v |= value << (entropy * off);
	buf[j] = v;
}

// inserts at specific index (last+1 expands)
static void insert(size_t index, int value) {
	int sign_switch = 0;
    // check how many bits we need
	int abs_mask = value >> (BITS_IN_INT - 1);
    int value_entropy = BITS_IN_INT - __builtin_clz(((value + abs_mask) ^ abs_mask) | 0x1);

    if (value < 0) {
        value_entropy++; // sign bit
        if (signed_values == 0) {
            sign_switch = 1;
        }
    }

    // force entropy to 32 bits after 16
    if (value_entropy > (BITS_IN_INT / 2)) {
        value_entropy = BITS_IN_INT - 1;
        if (signed_values == 0) {
			sign_switch = 1;
		}
    }

    // re-adjust previous bits if necessary
    if ((entropy < value_entropy) || sign_switch) {
		unsigned int *new_buffer = NULL;
        int new_buffer_size = size / VALS_IN_INT(value_entropy) + 1;
		new_buffer = malloc(sizeof(int) * new_buffer_size);
		allocated = sizeof(int) * new_buffer_size;
		memset(new_buffer, 0x0, sizeof(int) * new_buffer_size);

		for (int i = 0; i < size; i++) {
			int v = _get(buffer, i, entropy);
			_put(new_buffer, i, v, value_entropy);
		}

		free(buffer);
		buffer = new_buffer;
		entropy = value_entropy;
		if (sign_switch)
			signed_values = 1;
	}

	if (index >= size) {
		int new_size = index / VALS_IN_INT(entropy) + 1;
		int new_allocated = sizeof(int) * new_size;
		unsigned int *new_buffer = malloc(new_allocated);
		memset(new_buffer, 0x0, new_allocated);
		memcpy(new_buffer, buffer, allocated);
		free(buffer);
		buffer = new_buffer;
		allocated = new_allocated;
		size = new_size * VALS_IN_INT(entropy);
	}

	_put(buffer, index, value, entropy);
}

// return value at index
int get(size_t index) {
    return _get(buffer, index, entropy);
}

// returns -1 for not found
// - idea from "Bit Twiddling Hacks: Determine if a word has a zero byte"
// - but instead uses arbitrary length bit strings instead of bytes
size_t find(int value) {
	int viint = VALS_IN_INT(entropy);
	unsigned int mask = (1 << entropy) - 1;
	unsigned int pattern = 0;
	unsigned int high_bit_mask = 0;
	unsigned int big_mask = (~0) >> (BITS_IN_INT - viint * entropy);

	for (int i = 0; i < viint; i++) {
		pattern |= value << (entropy * i);
		high_bit_mask |= (mask >> 1) << (entropy * i);
	}
	
	int n = allocated / sizeof(int);
	for (int i = 0; i < n; i++) {
		int packed = buffer[i];
		unsigned int match = packed ^ pattern;
		int matches = ~((((match & high_bit_mask) + high_bit_mask) | match) | high_bit_mask);
		matches &= big_mask;
		
		if (matches) {
			for (int j = 0; j < viint; j++) {
				if (((packed >> (entropy * j)) & mask) == value)
					return i * viint + j;
			}
		}
	}

    return -1;
}

int max_value() {
    int max = (unsigned) (1 << (BITS_IN_INT - 1));
	for (int i = 0; i < size; i++) {
		int v = _get(buffer, i, entropy);
		if (max < v)
			max = v;
	}
    return max;
}

int min_value() {
    int min = (unsigned) (1 << (BITS_IN_INT - 1)) - 1;
	for (int i = 0; i < size; i++) {
		int v = _get(buffer, i, entropy);
		if (min > v)
			min = v;
	}
    return min;
}

void print_stat() {	
	printf("status: capacity %d, entropy %d bits, allocated %d bytes, max %d, min %d\n",
        size, entropy, allocated, max_value(), min_value());
}

void print_dump() {
	for (int i = 0; i < size; i++) {
		int v = _get(buffer, i, entropy);
		printf("%d,", v);
	}
	printf("\n");
}

// system time in ns
static long systemTime() {
    struct timespec t;
    t.tv_sec = t.tv_nsec = 0;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000000 + t.tv_nsec / 1000;
}

int main(void) {
	const int loop = 65536;
	int failed = 0;
	long t1, t2;

    srand(time(NULL));

	// sanity run
	init();
	t1 = systemTime();
    for (int i = 0; i < loop; i++)
        insert(i, i);
	t2 = systemTime();
	for (int i = 0; i < loop; i++) {
		if (get(i) != i) {
			if (!failed)
				printf("error at [ %d (%d) .. ", i, get(i));
			failed = 1;
		} else {
			if (failed)
				printf("%d (%d) %d (%d) )\n", i - 1, get(i-1), i, get(i));
			failed = 0;
		}
	}
	print_stat();
	int v = loop - 1;
	t1 = systemTime();
	int p = find(v);
	if (get(p) != v)
		printf("failed to find!\n");
	t2 = systemTime();
	printf("find took %ldns [%d]=%d\n", t2 - t1, p, get(p));

	t1 = systemTime();
    for (int i = 0; i < loop; i++)
        insert(i, -i);
	for (int i = 0; i < loop; i++)
		if (get(i) != -i) {
			if (!failed)
				printf("error at [ %d (%d)..", i, get(i));
			failed = 1;
		} else {
			if (failed)
				printf(".. %d (%d) )\n", i , get(i));
			failed = 0;
		}
    print_stat();
    deinit();

	// example runs
    init();
    for (int i = 0; i < loop; i++)
        insert(i, rand() % 2);
    print_stat();
    deinit();

    init();
    for (int i = 0; i < loop; i++)
        insert(i, (rand() % 15) + 1);
	printf("find(%d) = %zu\n", 15, find(15));
    print_stat();
    deinit();

    init();
    for (int i = 0; i < loop; i++)
        insert(i, (rand() % 511) - 255);
    printf("find(%d) = %zu\n", 255, find(255));
	print_stat();
    deinit();
	
    return 0;
}
