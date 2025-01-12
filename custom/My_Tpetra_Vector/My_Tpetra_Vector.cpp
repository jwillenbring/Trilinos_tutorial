//
// Example: Creating distributed Tpetra vectors.
//

#include <Tpetra_DefaultPlatform.hpp>
#include <Tpetra_Vector.hpp>
#include <Tpetra_Version.hpp>
#include <Teuchos_GlobalMPISession.hpp>
#include <Teuchos_oblackholestream.hpp>

void
exampleRoutine (const Teuchos::RCP<const Teuchos::Comm<int> >& comm,
                std::ostream& out)
{
  using std::endl;
  using Teuchos::Array;
  using Teuchos::ArrayView;
  using Teuchos::RCP;
  using Teuchos::rcp;

  // Print out the Tpetra software version information.
  out << Tpetra::version() << endl << endl;
  const int number = 7; 
  const char* command = "echo helloooo!";
  out << "This is a test" << endl;
  out << system(command);
  //
  // The first thing you might notice that makes Tpetra objects
  // different than their Epetra counterparts, is that Tpetra objects
  // take several template parameters.  These template parameters give
  // Tpetra its features of being able to solve very large problems
  // (of more than 2 billion unknowns) and to exploit intranode
  // parallelism.
  //
  // It's common to begin a Tpetra application with some typedefs to
  // make the code more concise and readable.  They also make the code
  // more maintainable, since you can change the typedefs without
  // changing the rest of the program.
  //

  // The "Scalar" type is the type of the values stored in the Tpetra
  // objects.  Valid Scalar types include real or complex
  // (std::complex<T>) floating-point types, or more exotic objects
  // with similar behavior.
  typedef double scalar_type;

  // The "LocalOrdinal" (LO) type is the type of "local" indices.
  // Both Epetra and Tpetra index local elements differently than
  // global elements.  Tpetra exploits this so that you can use a
  // shorter integer type for local indices.  This saves bandwidth
  // when computing sparse matrix-vector products.
  typedef int local_ordinal_type;

  // The "GlobalOrdinal" (GO) type is the type of "global" indices.
  typedef long global_ordinal_type;

  // The Kokkos "Node" type describes the type of shared-memory
  // parallelism that Tpetra will use _within_ an MPI process.  The
  // available Node types depend on Trilinos' build options and the
  // availability of certain third-party libraries.  Here are a few
  // examples:
  //
  // Kokkos::SerialNode: No parallelism
  //
  // Kokkos::TPINode: Uses a custom Pthreads wrapper
  //
  // Kokkos::TBBNode: Uses Intel's Threading Building Blocks
  //
  // Kokkos::ThrustNode: Uses Thrust, a C++ CUDA wrapper,
  //   for GPU parallelism.
  //
  // Using a GPU-oriented Node means that Tpetra objects that store a
  // lot of data (vectors and sparse matrices, for example) will store
  // that data on the GPU, and operate on it there whenever possible.
  //
  // Kokkos::DefaultNode gives you a default Node type.  It may be
  // different, depending on Trilinos' build options.  Currently, for
  // example, building Trilinos with Pthreads enabled gives you
  // Kokkos::TPINode by default.  That means your default Node is a
  // parallel node!
  typedef Kokkos::DefaultNode::DefaultNodeType node_type;

  // Maps know how to convert between local and global indices, so of
  // course they are templated on the local and global Ordinal types.
  // They are also templated on the Kokkos Node type, because Tpetra
  // objects that use Tpetra::Map are.  It's important not to mix up
  // Maps for different Kokkos Node types.
  typedef Tpetra::Map<local_ordinal_type, global_ordinal_type, node_type> map_type;

  // Get a pointer to the default Kokkos Node.  We'll need this when
  // creating the Tpetra::Map objects.  
  //
  // Currently, if you want a node of a different type, you have to
  // instantiate it explicitly.  We are moving to a model whereby you
  // template your entire program on the Node type, and the system
  // gives you the appropriate Node instantiation.
  RCP<node_type> node = Kokkos::DefaultNode::getDefaultNode ();

  //////////////////////////////////////////////////////////////////////
  // Create some Tpetra Map objects
  //////////////////////////////////////////////////////////////////////

  //
  // Like Epetra, Tpetra has local and global Maps.  Local maps
  // describe objects that are replicated over all participating MPI
  // processes.  Global maps describe distributed objects.  You can do
  // imports and exports between local and global maps; this is how
  // you would turn locally replicated objects into distributed
  // objects and vice versa.
  //

  // The total (global, i.e., over all MPI processes) number of
  // elements in the Map.  Tpetra's global_size_t type is an unsigned
  // type and is at least 64 bits long on 64-bit machines.
  //
  // For this example, we scale the global number of elements in the
  // Map with the number of MPI processes.  That way, you can run this
  // example with any number of MPI processes and every process will
  // still have a positive number of elements.
  const Tpetra::global_size_t numGlobalElements = comm->getSize() * 5;

  // Tpetra can index the elements of a Map starting with 0 (C style),
  // 1 (Fortran style), or any base you want.  1-based indexing is
  // handy when interfacing with Fortran.  We choose 0-based indexing
  // here.
  const global_ordinal_type indexBase = 0;

  // Construct a Map that puts the same number of equations on each
  // processor.  Pass in the Kokkos Node, so that this line of code
  // will work with any Kokkos Node type.
  //
  // It's typical to create a const Map.  Maps should be considered
  // immutable objects.  If you want a new data distribution, create a
  // new Map.
  RCP<const map_type> contigMap = 
    rcp (new map_type (numGlobalElements, indexBase, comm, 
                       Tpetra::GloballyDistributed, node));

  // contigMap is contiguous by construction.
  TEST_FOR_EXCEPTION(! contigMap->isContiguous(), std::logic_error,
                     "The supposedly contiguous Map isn't contiguous.");

  // Let's create a second Map.  It will have the same number of
  // global elements per process, but will distribute them
  // differently, in round-robin (1-D cyclic) fashion instead of
  // contiguously.
  RCP<const map_type> cyclicMap;
  {
    // We'll use the version of the Map constructor that takes, on
    // each MPI process, a list of the global elements in the Map
    // belonging to that process.  You can use this constructor to
    // construct an overlapping (also called "not 1-to-1") Map, in
    // which one or more elements are owned by multiple processes.  We
    // don't do that here; we make a nonoverlapping (also called
    // "1-to-1") Map.
    Array<global_ordinal_type>::size_type numEltsPerProc = 5;
    Array<global_ordinal_type> elementList (numEltsPerProc);

    const int numProcs = comm->getSize();
    const int myRank = comm->getRank();
    for (Array<global_ordinal_type>::size_type k = 0; k < numEltsPerProc; ++k)
      elementList[k] = myRank + k*numProcs;
    
    cyclicMap = rcp (new map_type (numGlobalElements, elementList, indexBase, 
                                   comm, node));
  }

  // If there's more than one MPI process in the communicator,
  // then cyclicMap is definitely NOT contiguous.
  TEST_FOR_EXCEPTION(comm->getSize() > 1 && cyclicMap->isContiguous(),
                     std::logic_error, 
                     "The cyclic Map claims to be contiguous.");

  // contigMap and cyclicMap should always be compatible.  However, if
  // the communicator contains more than 1 process, then contigMap and
  // cyclicMap are NOT the same.
  TEST_FOR_EXCEPTION(! contigMap->isCompatible (*cyclicMap),
                     std::logic_error,
                     "contigMap should be compatible with cyclicMap, "
                     "but it's not.");
  TEST_FOR_EXCEPTION(comm->getSize() > 1 && contigMap->isSameAs (*cyclicMap),
                     std::logic_error,
                     "contigMap should be compatible with cyclicMap, "
                     "but it's not.");

  //////////////////////////////////////////////////////////////////////
  // We have maps now, so we can create vectors.
  //////////////////////////////////////////////////////////////////////

  // Since Tpetra::Vector takes four template parameters, its type is
  // long.  It's helpful to use a typedef.  C++0x's type inference
  // feature is also useful.
  typedef Tpetra::Vector<scalar_type, local_ordinal_type, 
    global_ordinal_type, node_type> vector_type;

  // Create a Vector with the contiguous Map.  This version of the
  // constructor will fill in the vector with zeros.
  RCP<vector_type> x = rcp (new vector_type (contigMap));

  // The copy constructor performs a deep copy.  
  // x and y have the same Map.
  RCP<vector_type> y = rcp (new vector_type (*x));

  // Create a Vector with the 1-D cyclic Map.  Calling the constructor
  // with false for the second argument leaves the data uninitialized,
  // so that you can fill it later without paying the cost of
  // initially filling it with zeros.
  RCP<vector_type> z = rcp (new vector_type (contigMap, false));

  // Set the entries of z to (pseudo)random numbers.  Please don't
  // consider this a good parallel pseudorandom number generator.
  z->randomize ();

  // Set the entries of x to all ones.

  // Using the ScalarTraits class ensures that the line of code below
  // will work for any valid Scalar type, even for complex numbers or
  // more exotic fields.
  x->putScalar (Teuchos::ScalarTraits<scalar_type>::one());

  const scalar_type alpha = 3.14159;
  const scalar_type beta = 2.71828;
  const scalar_type gamma = -10;

  // x = beta*x + alpha*z
  //
  // This is a legal operation!  Even though the Maps of x and z are
  // not the same, their Maps are compatible.
  x->update (alpha, *z, beta);

  y->putScalar (42);
  // y = gamma*y + alpha*x + beta*z
  y->update (alpha, *x, beta, *z, gamma);
  
  // Compute the 2-norm of y.  
  //
  // The norm may have a different type than Scalar.  For example, if
  // Scalar is complex, then the norm is real.  We can use the traits
  // class to get the type of the norm.
  typedef Teuchos::ScalarTraits<scalar_type>::magnitudeType magnitude_type;
  const magnitude_type theNorm = y->norm2 ();

  // Print the norm of y on Proc 0.
  out << "Norm of y: " << theNorm << endl;

  // I added this code!
  out << "This is my new Code!" << endl;

  const magnitude_type theXNorm = x->norm2 ();
//  out << "x is: " << x << endl;
  out << "Norm of x?: " << theXNorm << endl;

  const magnitude_type theZNorm = z->norm2 ();
//  out << "z is: " << z << endl;
  out << "Norm of z?: " << theZNorm << endl;
}

//
// The same main() driver routine as in the TpetraInit example.
//
int 
main (int argc, char *argv[]) 
{
  using std::endl;
  using Teuchos::RCP;

  Teuchos::oblackholestream blackHole;
  Teuchos::GlobalMPISession mpiSession (&argc, &argv, &blackHole);
  RCP<const Teuchos::Comm<int> > comm = 
    Tpetra::DefaultPlatform::getDefaultPlatform().getComm();

  const int myRank = comm->getRank();
  std::ostream& out = (myRank == 0) ? std::cout : blackHole;

  // We have a communicator and an output stream.
  // Let's do something with them!
  exampleRoutine (comm, out);

  return 0;
}
