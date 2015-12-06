/* Notes: */
#include <cstdio>
#include <getopt.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <inttypes.h>
#include <sys/stat.h>
#include <NTL/ZZ_pX.h>
using namespace NTL;

static const char* usage =
"Usage: %s [OPTIONS] [FILES]\n"
"Secret sharing scheme.\n\n"
"   --nshares  NUM    generate NUM shares.\n"
"   --thresh   NUM    require NUM shares to reconstruct.\n"
"   --outdir   DIR    write shares in DIR.\n"
"   --help            show this message and exit.\n\n"
"NOTE: reconstruction will take place whenever multiple files\n"
"are given on the command line.\n";

void distribute(size_t n, size_t t, unsigned char* data,
				size_t len, vec_ZZ_p& shares);

/* buf is allocated here; freed by caller. nBytes stores the
 * number of bytes read. return value < 0 indicates failure. */
int readall(int fd, unsigned char*& buf, size_t& nBytes)
{
	size_t bsize = 1024;
	size_t chunk = bsize;
	buf = (unsigned char*)malloc(bsize);
	ssize_t n;
	nBytes = 0;
	do {
		if (bsize < nBytes+chunk) {
			bsize *= 2;
			buf = (unsigned char*)realloc(buf,bsize);
			if (!buf) return -1;
		}
		n = read(fd, buf+nBytes, chunk);
		if (n < 0 && errno == EINTR) continue;
		if (n < 0 && errno == EWOULDBLOCK) continue;
		if (n < 0) return -1;
		nBytes += n;
	} while (n);
	return 0;
}

int main(int argc, char *argv[]) {
	static int help = 0;
	// define long options
	static struct option long_opts[] = {
		{"nshares", required_argument, 0, 'n'},
		{"thresh",  required_argument, 0, 't'},
		{"outdir",  required_argument, 0, 'o'},
		{"help",    no_argument,       &help,  1},
		{0,0,0,0}
	};
	// process options:
	size_t n = 3, t = 2; /* default to 2 out of 3. */
	char outdir[256]; memset(outdir,0,256);
	char c;
	int opt_index = 0;
	while ((c = getopt_long(argc, argv, "n:t:o:", long_opts, &opt_index)) != -1) {
		switch (c) {
			case 'n':
				n = atol(optarg);
				break;
			case 't':
				t = atol(optarg);
				break;
			case 'o':
				strncpy(outdir,optarg,225); /* save space for share index */
				break;
			case '?':
				printf(usage,argv[0]);
				return 1;
		}
	}

	if (help) {
		printf(usage,argv[0]);
		return 0;
	}
	if (n < t) {
		fprintf(stderr,
				"number of shares (n=%lu) must be > threshold (t=%lu)\n",
				n,t);
		return 1;
	}

	vec_ZZ_p shares;
	if (argc - optind < 2) {
		/* if no files given, read stdin */
		FILE* f = (argc == optind) ? stdin : fopen(argv[optind++],"rb");
		if (!f) {
			fprintf(stderr, "error opening input file\n");
			return 1;
		}
		int fd = fileno(f);
		/* NOTE: we read entire contents into buffer.  Have to do arithmetic
		 * with this value anyway, so it's difficult to avoid having it all
		 * in memory at some point. */
		unsigned char* buf;
		size_t inSize;
		readall(fd,buf,inSize);
		distribute(n,t,buf,inSize,shares);
		free(buf);
		fclose(f);
		/* now write shares to temporary directory. */
		if (outdir[0] == 0) {
			strcpy(outdir,"/tmp/shares-XXXXXX");
			mkdtemp(outdir);
		}
		size_t fnameoffset = strnlen(outdir,225);
		/* shares will take one more byte than inSize, so to be safe, allocate
		 * another array: */
		unsigned char* sbuf = new unsigned char[inSize+1];
		for (size_t i = 0; i < n; i++) {
			BytesFromZZ(sbuf,rep(shares[i]),inSize+1);
			/* share = poly evaluated at filename: */
			sprintf(outdir+fnameoffset,"/%lu",i+1);
			/* set permissions conservatively: */
			int fd = open(outdir,O_RDWR|O_CREAT|O_TRUNC,0600);
			f = fdopen(fd,"wb");
			/* format: [x||f(x)], where x takes 4 bytes. */
			uint32_t x = i+1;
			fwrite(&x,1,sizeof(x),f);
			fwrite(sbuf,1,inSize+1,f);
			fclose(f);
		}
		delete [] sbuf;
	} else {
		/* all the shares should have the same length. figure that out
		 * now by stat'ing the first input. */
		t = argc - optind; /* set t to the number of shares supplied. */
		uint32_t x;
		vec_ZZ_p inputs;
		struct stat s;
		int fd = open(argv[optind],O_RDONLY);
		if (fd == -1 || fstat(fd,&s) == -1) {
			fprintf(stderr, "couldn't stat %i\n",fd);
			return -1;
		}
		/* get length of f(x) */
		size_t slen = s.st_size - sizeof(x);
		close(fd);
		/* set the prime: */
		ZZ lb = (ZZ(1L) << (8*(slen-1))); /* lower bound */
		ZZ p = NextPrime(lb);
		ZZ_p::init(p);
		/* first 4 bytes will contain x, the rest will contain f(x) */
		unsigned char* sbuf = new unsigned char[slen];
		ZZ temp; /* for reading shares from bytes */
		for (size_t i = optind; i < (size_t)argc; i++) {
			/* read a share from the input file */
			FILE* f = fopen(argv[i],"rb");
			fread(&x,1,sizeof(x),f);
			inputs.append(ZZ_p(x));
			fread(sbuf,1,slen,f);
			fclose(f);
			ZZFromBytes(temp,sbuf,slen);
			shares.append(conv<ZZ_p>(temp));
		}
		/* sbuf should have enough space for the secret; reuse it. */
		/* NOTE: reconstruction is not its own function since it currently
		 * amounts to two NTL function calls once you've read the shares: */
		ZZ_pX f;
		interpolate(f,inputs,shares);
		BytesFromZZ(sbuf,rep(f[0]),slen);
		/* NOTE: last byte should be 0.  Could serve as null char for
		 * a string; should be ignored for raw data. */
		fwrite(sbuf,1,slen-1,stdout);
		delete [] sbuf;
	}
	return 0;
}

// http://blog.bjrn.se/2008/09/speeding-up-haskell-with-c-very-short.html
// https://wiki.haskell.org/Foreign_Function_Interface
// http://stackoverflow.com/questions/9854782/can-the-ffi-deal-with-arrays-if-so-how

/* NOTE: The haskell FFI does not sound particularly fun for something like NTL
 * which will have many different structures (in C++!) that would have to be
 * accounted for.  If you really do want to start, I suppose ZZ or ZZ_p would
 * be a natural choice.  Tough to do much else without those. */

void distribute(size_t n, size_t t, unsigned char* data,
				size_t len, vec_ZZ_p& shares) {
	/* NOTE: *convention on primes*  We will use the first prime that is
	 * longer than the length of the input.  More specifically, if the input
	 * is k bytes, we will pick the next prime larger than 2**(8*k).  */

	ZZ lb = (ZZ(1L) << (8*len)); /* lower bound for p */
	ZZ p = NextPrime(lb);
	ZZ_p::init(p);
	/* set seed: */
	FILE* frand = fopen("/dev/urandom","rb");
	unsigned char seed[32];
	fread(seed,1,32,frand);
	fclose(frand);
	SetSeed(seed,32);
	ZZ_pX f = random_ZZ_pX(t); /* degree will be t-1, whp */
	/* set f[0] to the secret. */
	ZZ s = ZZFromBytes(data,len);
	ZZ_p sp(conv<ZZ_p>(s));
	SetCoeff(f,0,sp);
	/* now evaluate at points 1..n */
	for (long i = 0; i < (long)n; i++)
		shares.append(eval(f,ZZ_p(i+1)));
	/* XXX: currently memory footprint is (n+t)*N where the secret
	 * is N bytes.  Could reduce it and keep the same interface by
	 * doing a parallel horner's rule to avoid the memory overhead
	 * of storing the entire polynomial (left with nN).  Alternatively
	 * we could rewind the PRG and do horner's rule for each share
	 * in sequence, leaving only N. */
}
