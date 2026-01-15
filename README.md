# A FUSE file system vending arbitrary volume of reproducible data in a file

This is a [FUSE (3)](https://github.com/libfuse/libfuse) application offering a file system. The file system presents _one single_ file (with the name of your choice, for convenience), backed by a [PRNG](http://en.wikipedia.org/wiki/Pseudorandom_number_generator) (by definition _deterministic_), vending from infinite series of bits. Currently the PRNG is [the Splitmix64 algorithm](http://rosettacode.org/wiki/Pseudo-random_numbers/Splitmix64) (specifically the `next_int` variant, under "Basic pseudocode algorithm"). This specific PRNG was chosen for its fast data generation performance, simplicity of implementation, and a sufficient _full period_. **Disclaimer**: this isn't a cryptographic application -- the PRNG is not meant to be used as a [CSPRNG](http://en.wikipedia.org/wiki/Cryptographically_secure_pseudorandom_number_generator).

You can think of the purpose of this project as making available a file much like the well-known [`/dev/random`](http://en.wikipedia.org/wiki//dev/random), except a) the file made available is meant to function as a so-called _regular_ file, _seekable_ and allowing random-access -- not e.g. a _device_ [file] that `/dev/random` is [categorised as], and b) the file produces the _same_ data for the same reading offset, every time -- as if it was a real, storage-backed file containing the specific series of bits. Again: the PRNG does not pass criteria for randomness sufficient for e.g. CSRNG -- for purposes of the file system it's of little consequence how "random" the data are, the only important properties of their distribution are such that minimise chance of accidentally writing wrong data at the right offset by an application being verified that relies on the file system.

Crucially, the file system allows _writing_ in the file that it makes available, with the important property that the data it permits _must_ match the corresponding part of the series, effectively requiring that what is written at an offset is identical to what was read from the same offset. This lends the file system utility in data verification scenarios, which was what prompted me to implement it in the first place.

In context of reading and writing, mounting of the file system allows you to specify the _initial_ size of the file, thus effectively capping the series past some offset at least as initially presented, but the file system does permit writing (appending) data _past_ the end of the file -- again, _iff_ the data attempted written would match what the corresponding portion of the file would contain as defined by the series. Conversely, reading past the end of the file will of course not produce any data (despite the series being infinite in general). Both of these properties follow how a regular file is expected to behave. The `--size` mounting option (`0` by default / omitted/ implied) allows variation on the kind of scenarios the file system may be used in.

## Usage

### Building

Build the program as per convention, using e.g. [GNU Make](http://www.gnu.org/software/make):

```console
make
```

This will produce `./mock-large-files-fuse`.

### Mounting the file system

Mount the filesystem, as per convention, using `./mock-large-files-fuse` and a _mountpoint_ of your choice (a path to an existing directory):

```console
./mock-large-files-fuse /mnt/mock-large-files-fuse --filename data
```

### Reading

The file system will "shadow" the path and make available a file named `data` directly at the mountpoint directory. The file by default is empty -- provide `--size` to `./mock-large-files-fuse` command line like above, with a value, to have an effectively _readable_ file instead. Here's reading the first 100 bytes (or however many available in the file, if there's fewer) in the series and printing them in hexadecimal format:

```console
xxd -l 100 /mnt/mock-large-files-fuse/data
```

### Writing

As explained earlier, writing to the file will fail unless the e.g. bytes you write are the same data that was read at the offset. The following will therefore produce an I/O error:

```console
echo 'Hello world' > /mnt/mock-large-files-fuse/data
```

The below variant, however, will succeed:

```console
head -c 100 /mnt/mock-large-files-fuse/data > /mnt/mock-large-files-fuse/data
```

### Installing with Python

Although this is a [relatively simple] C application project through and through -- which naturally does not require Python -- because I am planning to at least offer an equivalent written in Python, I thought that presence on [PyPi](http://pypi.org) as a package would help with distribution of the software. Building the "wheel" implies and therefore poses the same requirements as for building the program (a C compiler and linker and Make). So does installing the program using PyPi, normally. Installation can be done conventionally:

1. From PyPi:

```console
pip install mock-large-files-fuse
```

2. From a Github repository:

```console
pip install git+ssh@git@github.example.com:owner/mock-large-files-fuse.git
```

## Performance

Clocked 3-4GiB/s reading from a 10TB-sized file on Linux 5.14 (`5.14.0-611.16.1.el9_7.x86_64`; `SMP`; `PREEMPT`) on Intel Core i7-6700. Disclaimer: yes, I am well aware this is nowhere near providing enough detail to make this a true benchmark report, but frankly I have no idea which parts of the machinery are a factor here -- between the power policy configured for the kernel, the Linux distribution (RHEL 9), and the version of `libfuse`, not to mention a plethora of other perfectly valid candidates. I just want to give you a taste of the _order_ of the performance, that is all.
