SLURM_BASE = /home/user/slurm/slurm-18.08
SLURM_INST = /sandbox/slurm

CFLAGS = -O0 -g

all: pmixp_2d_ring

pmixp_2d_ring: pmixp_2d_ring.c
	gcc $(PMIX_INC) $(CFLAGS) -o pmixp_2d_ring  pmixp_2d_ring.c -I$(SLURM_BASE) -Wl,-rpath,$(SLURM_INST)/lib -L$(SLURM_INST)/lib -lslurm

clean:
	rm -f pmixp_2d_ring
