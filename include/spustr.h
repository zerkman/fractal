/* this is a shared data structure for parameter communication between the PPU
 * and each SPU. The structure address in main memory for each SPU
 * is transmitted as an argument of the main function. The SPU fetches the
 * contents of this structure at every frame.
 */
typedef struct {
	uint32_t id;		/* spu thread id */
	uint32_t rank;		/* rank in SPU thread group (0..count-1) */
	uint32_t count;		/* number of threads in group */
	uint32_t sync;		/* sync value for response */
	uint32_t response;	/* response value */
	uint32_t width;		/* screen width in pixels */
	uint32_t height;	/* screen width in pixels */
	float zoom;		/* zoom factor */
	float xc;		/* x center */
	float yc;		/* y center */
	uint32_t dummy[2];	/* unused data for 16-byte multible size alignment */
} spustr_t;
