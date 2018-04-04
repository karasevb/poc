SLURM_INST=/sandbox/slurm
SLURM_SRC=/home/user/slurm/slurm-17-11-0-0rc2
PMIX_INST=/home/user/pmix/v2.1/install
OMPI_INST=/home/user/ompi/openmpi-v3.1.x-201803140301-169afcd/install

CFLAGS= -Wall -I$(SLURM_SRC) -I$(SLURM_SRC)/src/plugins/mpi/pmix -I${OMPI_INST}/include
LDFLAGS= -Wl,-rpath=$(SLURM_INST)/lib:$(SLURM_INST)/lib/slurm -g -ldl -lpthread

SLURM_LIB= -L$(SLURM_INST)/lib -lslurm -l:mpi_pmix_v2.so -L$(SLURM_INST)/lib/slurm -lslurmfull
#PMIX_LIB= -L$(PMIX_INST)/lib -lpmix

CC=mpicc

all: coll_ring

pmixp_coll_ring.o: pmixp_coll_ring.c
	$(CC) $(CFLAGS)  $(LDFLAGS) -c pmixp_coll_ring.c

coll_ring: pmixp_coll_ring.o
	$(CC) pmixp_coll_ring.o $(CFLAGS) $(LDFLAGS) $(SLURM_LIB) $(PMIX_LIB) -o coll_ring

clean:
	rm -f pmixp_coll_ring.o coll_ring
