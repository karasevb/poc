SLURM_INST=/sandbox/slurm
SLURM_SRC=/home/user/slurm/slurm-17-11-0-0rc2
PMIX_INST=/home/user/pmix/v2.1/install
PMIX_BASE=/home/user/pmix/v2.1/
OMPI_INST=/home/user/ompi/openmpi-v3.1.x-201803140301-169afcd/install
LIBEV_BASE=/home/user/pmix/libevent-2.0.22-stable

CFLAGS= -Wall -I$(SLURM_SRC) -I$(SLURM_SRC)/src/plugins/mpi/pmix -I${OMPI_INST}/include -g -O0
LDFLAGS= -Wl,-rpath=$(SLURM_INST)/lib:$(SLURM_INST)/lib/slurm -g -O0 -ldl -lpthread

SLURM_LIB= -L$(SLURM_INST)/lib -lslurm -l:mpi_pmix_v2.so -L$(SLURM_INST)/lib/slurm -lslurmfull
PMIX_LIB= -Wl,-rpath=$(PMIX_INST)/lib -L$(PMIX_INST)/lib -lpmix -I$(PMIX_BASE)/src/include
LIBEV_LIB= -Wl,-rpath=$(LIBEV_BASE)/install/lib -levent -I$(LIBEV_BASE)/install/include

CC=mpicc

all: coll_ring fence_test

%.o : %.c
	$(CC) -c $(CFLAGS) $< -o $@

coll_ring: pmixp_coll_ring.o
	$(CC) pmixp_coll_ring.o $(CFLAGS) $(LDFLAGS) $(SLURM_LIB) -o coll_ring

fence_test.o: fence_test.c
	$(CC) -c $(CFLAGS) $(LDFLAGS) $(PMIX_LIB) -I$(PMIX_INST)/include -I$(PMIX_BASE) $(LIBEV_LIB) fence_test.c

fence_test: fence_test.o
	$(CC) fence_test.o  $(CFLAGS) $(LDFLAGS) $(PMIX_LIB) -I$(PMIX_INST)/include -I$(PMIX_BASE)  $(LIBEV_LIB) -o fence_test

clean:
	rm -f *.o coll_ring fence_test
