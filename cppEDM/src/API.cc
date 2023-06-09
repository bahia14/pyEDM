
//----------------------------------------------------------------
// Functions implemented here:
//     Embed(), MakeBlock(), Simplex(), SMap(), CCM(), Multiview()
//
// Functions implemented in Eval.cc:
//     EmbedDimension(), PredictInterval(), PredictNonlinear()
//
// NOTE: Functions that implement either filepath or DataFrame
//       input are overloads. The first pattern takes a filepath
//       argument, creates the DataFrame object, then calls the
//       second with a reference to the DataFrame.
//
//       In the case of SMap, there are 4 overloads, two with
//       the default SVD solver, two with a user supplied solver.
//       In all cases, the final overload (4) creates the SMap
//       object and executes the SMap algorithm.
//----------------------------------------------------------------

#include "API.h"

//----------------------------------------------------------------
// Embed from file path/file input
//----------------------------------------------------------------
DataFrame< double > Embed( std::string path,
                           std::string dataFile,
                           int         E,       // embedding dimension
                           int         tau,     // time step offset
                           std::string columns, // column names or indices
                           bool        verbose ) {

    DataFrame< double > dataFrame( path, dataFile );
    DataFrame< double > embedded = Embed( std::ref( dataFrame ),
                                          E, tau, columns, verbose ); 
    return embedded;
}

//----------------------------------------------------------------
// Embed from DataFrame input
//----------------------------------------------------------------
DataFrame< double > Embed( DataFrame< double > & dataFrameIn,
                           int                 E,
                           int                 tau,
                           std::string         columns,
                           bool                verbose ) {

    // Parameter.Validate will convert columns into a vector of names
    // or a vector of column indices
    Parameters parameters = Parameters( Method::Embed, "", "", "", "",
                                        "1 1", "1 1", E, 0, 0, tau, 0, 0,
                                        columns, "", false, false, verbose );
    // Instantiate EDM object
    EDM EDM_Embed = EDM( dataFrameIn, std::ref( parameters ) );

    // Perform embedding : calls MakeBlock() API function
    EDM_Embed.EmbedData();

    return EDM_Embed.embedding;
}

//------------------------------------------------------------------------
// MakeBlock from dataFrame :: API function
// First (or last) tau * (E-1) dataFrame rows have partial data,
// deletePartial controls whether or not they are returned.
// Does not validate parameters or columns, use EmbedData()
//------------------------------------------------------------------------
DataFrame< double > MakeBlock( DataFrame< double >      & dataFrame,
                               int                      E,
                               int                      tau,
                               std::vector<std::string> columnNames,
                               bool                     deletePartial )
{
    if ( columnNames.size() != dataFrame.NColumns() ) {
        std::stringstream errMsg;
        errMsg << "MakeBlock: The number of columns in the dataFrame ("
               << dataFrame.NColumns() << ") is not equal to the number "
               << "of columns specified (" << columnNames.size() << ").\n";;
        throw std::runtime_error( errMsg.str() );
    }

    if ( E < 1 ) {
        std::stringstream errMsg;
        errMsg << "MakeBlock(): E = " << E << " is invalid.\n" ;
        throw std::runtime_error( errMsg.str() );
    }

    size_t NDataRows = dataFrame.NRows();        // number of input rows
    size_t NPartial  = abs( tau ) * (E-1);       // rows of partial data
    size_t NRowOut;                              // number of output rows
    size_t NColOut   = dataFrame.NColumns() * E; // number of output columns

    // Create embedded data frame column names X(t-0) X(t-1)...
    std::vector< std::string > newColumnNames( NColOut );
    size_t newCol_i = 0;
    for ( size_t col = 0; col < columnNames.size(); col ++ ) {
        for ( int e = 0; e < E; e++ ) {
            std::stringstream ss;
            if ( tau < 0 ) {
                ss << columnNames[ col ] << "(t-" << -tau * e << ")";
            }
            else {
                ss << columnNames[ col ] << "(t+" << tau * e << ")";
            }
            newColumnNames[ newCol_i ] = ss.str();
            newCol_i++;
        }
    }

    // Number of rows of output data frame
    if ( deletePartial ) {
        if ( NPartial >= NDataRows ) {
            std::stringstream errMsg;
            errMsg << "MakeBlock(): Number of data rows " << NDataRows
                   << " not sufficient for removal of "   << NPartial
                   << " rows [tau*(E-1)] of partial embedding vectors.\n" ;
            throw std::runtime_error( errMsg.str() );
        }
        NRowOut = NDataRows - NPartial;
    }
    else {
        NRowOut = NDataRows;
    }

    // Ouput data frame
    DataFrame< double > embedding( NRowOut, NColOut, newColumnNames );

    // To keep track of where to insert column in new data frame
    size_t colCount = 0;

    std::slice slice_i;   // slice to write data rows
    std::slice slice_NA;  // slice for partial data
    std::valarray< double > rowNan; // NAN for partial data if not deletePartial

    if ( deletePartial ) {
        if ( tau < 0 ) {
            slice_i = std::slice( NPartial, NDataRows - NPartial, 1 );
        }
        else {
            slice_i = std::slice( 0, NDataRows - NPartial, 1 );
        }
    }
    else {
        slice_i = std::slice( 0, NDataRows, 1 );
        rowNan  = std::valarray< double >( NAN, NRowOut );
    }

    // Shift column data and write to embedding data frame
    for ( size_t col = 0; col < dataFrame.NColumns(); col++ ) {
        // for each embedding dimension
        for ( int e = 0; e < E; e++ ) {

            std::valarray< double > column = dataFrame.Column( col );

            // Returns a copy of the valarray object with its elements
            // shifted left n spaces (or right if n is negative).
            std::valarray< double > tmp = column.shift( e * tau );

            if ( not deletePartial ) { // replace shift 0's with NaN
                int N = e * abs( tau );
                if ( tau < 0 ) {
                    slice_NA = std::slice( 0, N, 1 );
                }
                else {
                    slice_NA = std::slice( NDataRows - N, N, 1 );
                }
                tmp[ slice_NA ] = rowNan[ slice_NA ];
            }

            // Write shifted columns to the output embedding DataFrame
            embedding.WriteColumn( colCount, tmp[ slice_i ] );

            colCount++;
        }
    }

    return embedding;
}

//----------------------------------------------------------------------
// Simplex with pathIn/dataFile input : calls Simplex( & DataFrame )
//----------------------------------------------------------------------
SimplexValues Simplex( std::string       pathIn,
                       std::string       dataFile,
                       std::string       pathOut,
                       std::string       predictFile,
                       std::string       lib,
                       std::string       pred,
                       int               E,
                       int               Tp,
                       int               knn,
                       int               tau,
                       int               exclusionRadius,
                       std::string       columns,
                       std::string       target,
                       bool              embedded,
                       bool              const_predict,
                       bool              verbose,
                       std::vector<bool> validLib,
                       int               generateSteps,
                       bool              generateLibrary,
                       bool              parameterList )
{
    // DataFrame constructor loads data
    DataFrame< double > DF( pathIn, dataFile );

    // Call Simplex( & DataFrame )
    SimplexValues S = Simplex( std::ref( DF ),
                               pathOut,
                               predictFile,
                               lib,
                               pred,
                               E,
                               Tp,
                               knn,
                               tau,
                               exclusionRadius,
                               columns,
                               target,
                               embedded,
                               const_predict,
                               verbose,
                               validLib,
                               generateSteps,
                               generateLibrary,
                               parameterList );

    return S;
}

//----------------------------------------------------------------------
// Simplex with DataFrame input
//----------------------------------------------------------------------
SimplexValues Simplex( DataFrame< double > & DF,
                       std::string       pathOut,
                       std::string       predictFile,
                       std::string       lib,
                       std::string       pred,
                       int               E,
                       int               Tp,
                       int               knn,
                       int               tau,
                       int               exclusionRadius,
                       std::string       columns,
                       std::string       target,
                       bool              embedded,
                       bool              const_predict,
                       bool              verbose,
                       std::vector<bool> validLib,
                       int               generateSteps,
                       bool              generateLibrary,
                       bool              parameterList )
{
    // Instantiate Parameters
    Parameters parameters = Parameters( Method::Simplex, "", "",
                                        pathOut, predictFile,
                                        lib, pred, E, Tp, knn, tau, 0,
                                        exclusionRadius,
                                        columns, target, embedded,
                                        const_predict, verbose, validLib,
                                        generateSteps, generateLibrary,
                                        parameterList );

    // Instantiate EDM::SimplexClass object
    SimplexClass SimplexModel = SimplexClass( DF, std::ref( parameters ) );

    if ( generateSteps ) {
        SimplexModel.Generate();
    }
    else {
        SimplexModel.Project();
    }

    SimplexValues values = SimplexValues();
    values.predictions   = SimplexModel.projection;
    values.parameterMap  = SimplexModel.parameters.Map;

    return values;
}

//----------------------------------------------------------------------------
// 1) SMap with pathIn/dataFile input. Calls overload 2)
//    Default SVD (LAPACK) assigned in SMap() overload 2)
//----------------------------------------------------------------------------
SMapValues SMap( std::string pathIn,
                 std::string dataFile,
                 std::string pathOut,
                 std::string predictFile,
                 std::string lib,
                 std::string pred,
                 int         E,
                 int         Tp,
                 int         knn,
                 int         tau,
                 double      theta,
                 int         exclusionRadius,
                 std::string columns,
                 std::string target,
                 std::string smapFile,
                 std::string derivatives,
                 bool        embedded,
                 bool        const_predict,
                 bool        verbose,
                 std::vector<bool> validLib,
                 int         generateSteps,
                 bool        generateLibrary,
                 bool        parameterList )
{
    // DataFrame constructor loads data
    DataFrame< double > DF( pathIn, dataFile );

    // Call overload 2) with DataFrame
    SMapValues SMapOutput = SMap( std::ref( DF ), pathOut, predictFile,
                                  lib, pred, E, Tp, knn, tau, theta,
                                  exclusionRadius,
                                  columns, target, smapFile, derivatives, 
                                  embedded, const_predict, verbose, validLib,
                                  generateSteps, generateLibrary,
                                  parameterList );
    return SMapOutput;
}

//----------------------------------------------------------------------------
// 2) SMap with DataFrame. Calls overload 4)
//    Default SVD (LAPACK) was assigned in Smap.cc overload 2)
//----------------------------------------------------------------------------
SMapValues SMap( DataFrame< double > & DF,
                 std::string pathOut,
                 std::string predictFile,
                 std::string lib,
                 std::string pred,
                 int         E,
                 int         Tp,
                 int         knn,
                 int         tau,
                 double      theta,
                 int         exclusionRadius,
                 std::string columns,
                 std::string target,
                 std::string smapFile,
                 std::string derivatives,
                 bool        embedded,
                 bool        const_predict,
                 bool        verbose,
                 std::vector<bool> validLib,
                 int         generateSteps,
                 bool        generateLibrary,
                 bool        parameterList )
{
    // Call overload 4) with default SVD function
    SMapValues SMapOutput = SMap( DF, pathOut, predictFile,
                                  lib, pred, E, Tp, knn, tau, theta, 
                                  exclusionRadius,
                                  columns, target, smapFile, derivatives,
                                  & SVD, // LAPACK SVD default
                                  embedded, const_predict, verbose, validLib,
                                  generateSteps, generateLibrary,
                                  parameterList );

    return SMapOutput;
}

//----------------------------------------------------------------------------
// 3) Data pathIn/dataFile with external solver object. Calls 4)
//----------------------------------------------------------------------------
SMapValues SMap( std::string pathIn,
                 std::string dataFile,
                 std::string pathOut,
                 std::string predictFile,
                 std::string lib,
                 std::string pred,
                 int         E,
                 int         Tp,
                 int         knn,
                 int         tau,
                 double      theta,
                 int         exclusionRadius,
                 std::string columns,
                 std::string target,
                 std::string smapFile,
                 std::string derivatives,
                 std::valarray< double > (*solver)(DataFrame < double >,
                                               std::valarray < double >),
                 bool        embedded,
                 bool        const_predict,
                 bool        verbose,
                 std::vector<bool> validLib,
                 int         generateSteps,
                 bool        generateLibrary,
                 bool        parameterList )
{
    // DataFrame constructor loads data
    DataFrame< double > DF( pathIn, dataFile );

    // Call overload 4) with DataFrame and solver object
    SMapValues SMapOutput = SMap( std::ref( DF ), pathOut, predictFile,
                                  lib, pred, E, Tp, knn, tau, theta,
                                  exclusionRadius,
                                  columns, target, smapFile, derivatives, 
                                  solver, embedded, const_predict, verbose,
                                  validLib, generateSteps, generateLibrary,
                                  parameterList );
    return SMapOutput;
}

//----------------------------------------------------------------------------
// 4) DataFrame with external solver object
//----------------------------------------------------------------------------
SMapValues SMap( DataFrame< double > & DF,
                 std::string pathOut,
                 std::string predictFile,
                 std::string lib,
                 std::string pred,
                 int         E,
                 int         Tp,
                 int         knn,
                 int         tau,
                 double      theta,
                 int         exclusionRadius,
                 std::string columns,
                 std::string target,
                 std::string smapFile,
                 std::string derivatives,
                 std::valarray< double > (*solver)(DataFrame < double >,
                                               std::valarray < double >),
                 bool        embedded,
                 bool        const_predict,
                 bool        verbose,
                 std::vector<bool> validLib,
                 int         generateSteps,
                 bool        generateLibrary,
                 bool        parameterList )
{
    if ( derivatives.size() ) {} // -Wunused-parameter

    Parameters parameters = Parameters( Method::SMap, "", "",
                                        pathOut, predictFile,
                                        lib, pred, E, Tp, knn, tau, theta,
                                        exclusionRadius,
                                        columns, target, embedded,
                                        const_predict, verbose, validLib,
                                        generateSteps, generateLibrary,
                                        parameterList, smapFile );

    // Handle nan
    // If nan are found in library or prediction rows of columns or target,
    // remove them from DataFrame DF
    //   nan rows are saved in DF nanRows, validRows
    //   JP: Seems silly to do this check as a default...
    std::vector< std::string > nanColsCheck = parameters.columnNames;
    // Add target to nanColsCheck for DF.NanRows()
    // Don't add empty or degenerate target
    if ( not parameters.targetNames.empty() and
         find( nanColsCheck.begin(), nanColsCheck.end(),
               parameters.targetNames.front() ) == nanColsCheck.end() ) {
        nanColsCheck.push_back( parameters.targetNames.front() );
    }
    bool nanFound = DF.NanRows( nanColsCheck ); // Look for nan

    // Reference to DF or DataFrameRemoveNanRows
    DataFrame< double > DF_ = std::ref( DF );

    if ( nanFound ) {
        // Remove DF nanRows from parameters.library and prediction
        // If any nan are found, set nanRemovedLibPred true
        bool nanRemovedLibPred = false;
        
        // First, sort nanRows to delete from lib/pred in reverse order
        std::sort( DF.NanRows().begin(), DF.NanRows().end() );

        // Now erase in reverse order
        std::vector< size_t >::iterator         vi;
        std::vector< size_t >::reverse_iterator ri;

        for ( ri = DF.NanRows().rbegin(); ri != DF.NanRows().rend(); ++ri ) {
            vi = std::find( parameters.library.begin(),
                            parameters.library.end(), *ri );
            if ( vi != parameters.library.end() ) {
                parameters.library.erase( vi );
                if ( not nanRemovedLibPred ){ nanRemovedLibPred = true; }
            }
            vi = std::find( parameters.prediction.begin(),
                            parameters.prediction.end(), *ri );
            if ( vi != parameters.prediction.end() ) {
                parameters.prediction.erase( vi );
                if ( not nanRemovedLibPred ){ nanRemovedLibPred = true; }
            }
        }

        if ( nanRemovedLibPred ) {
            // JP: If DF is large, it is dumb to create a new DF
            DataFrame< double > DFNanRemove = DF.DataFrameRemoveNanRows();
            DF_ = std::ref( DFNanRemove );
            
            std::stringstream msg;
            msg << "WARNING: SMap() nan rows detected in columns or target. "
                << DF.NanRows().size() << " deleted. "
                << "Original number of rows " << DF.NRows() << ".\n";
            if ( not parameters.embedded ) {
                msg << "Time delay embedding presumption violated.\n";
            }
            std::cout << msg.str();
        }
    }

    // Instantiate EDM::SMapClass object
    SMapClass SMapModel = SMapClass( DF_, std::ref( parameters ) );

    if ( generateSteps ) {
        SMapModel.Generate( solver );
    }
    else {
        SMapModel.Project( solver );
    }

    SMapValues values   = SMapValues();
    values.predictions  = SMapModel.projection;
    values.coefficients = SMapModel.coefficients;
    values.parameterMap = SMapModel.parameters.Map;

    return values;
}

//----------------------------------------------------------------------
// CCM with pathin/dataFile input. Calls CCM( & DF )
//----------------------------------------------------------------------
CCMValues CCM( std::string pathIn,
               std::string dataFile,
               std::string pathOut,
               std::string predictFile,
               int         E,
               int         Tp,
               int         knn,
               int         tau,
               int         exclusionRadius,
               std::string columns,
               std::string target,
               std::string libSizes_str,
               int         sample,
               bool        random,
               bool        replacement,
               unsigned    seed,
               bool        embedded,
               bool        includeData,
               bool        parameterList,
               bool        verbose )
{
    // DataFrame constructor loads data
    DataFrame< double > DF( pathIn, dataFile );

    CCMValues ccmValues = CCM( std::ref( DF ), pathOut, predictFile,
                               E, Tp, knn, tau, exclusionRadius,
                               columns, target, libSizes_str,
                               sample, random, replacement,
                               seed, embedded, includeData,
                               parameterList, verbose );

    return ccmValues;
}

//----------------------------------------------------------------------
// CCM with DataFrame input
//----------------------------------------------------------------------
CCMValues CCM( DataFrame< double > & DF,
               std::string pathOut,
               std::string predictFile,
               int         E,
               int         Tp,
               int         knn,
               int         tau,
               int         exclusionRadius,
               std::string columns,
               std::string target,
               std::string libSizes_str,
               int         sample,
               bool        random,
               bool        replacement,
               unsigned    seed,
               bool        embedded,
               bool        includeData,
               bool        parameterList,
               bool        verbose )
{
    // Set library and prediction indices to entire library (embedded)
    std::stringstream ss;
    ss << "1 " << DF.NRows();

    Parameters parameters = Parameters( Method::CCM,
                                        "",              // pathIn
                                        "",              // dataFile
                                        pathOut,         // 
                                        predictFile,     // 
                                        ss.str(),        // lib_str
                                        ss.str(),        // pred_str
                                        E,               // 
                                        Tp,              // 
                                        knn,             // 
                                        tau,             // 
                                        0,               // theta
                                        exclusionRadius, //
                                        columns,         // 
                                        target,          // 
                                        embedded,        // 
                                        false,           // const_predict
                                        verbose,         // 
                                        std::vector<bool>(), // validLib
                                        0,               // generateSteps
                                        false,           // generateLibrary
                                        parameterList,   //
                                        "",              // SmapFile
                                        "",              // blockFile
                                        0,               // multiviewEnsemble
                                        0,               // multiviewD
                                        false,           // multiviewTrainLib
                                        false,           // multiviewExcludeTarg
                                        libSizes_str,    // 
                                        sample,          // 
                                        random,          // 
                                        replacement,     // 
                                        seed,            //
                                        includeData );   //

    // Instantiate EDM::Simplex::CCM object
    CCMClass CCMModel = CCMClass( DF, std::ref( parameters ) );

    CCMModel.Project();

    CCMValues values    = CCMValues();
    values.AllLibStats  = CCMModel.allLibStats;
    values.CrossMap1    = CCMModel.colToTargetValues;
    values.CrossMap2    = CCMModel.targetToColValues;
    values.parameterMap = CCMModel.parameters.Map;

    return values;
}

//----------------------------------------------------------------------
// Multiview with path/file input. Calls Multiview( & DF )
//----------------------------------------------------------------------
MultiviewValues Multiview( std::string pathIn,
                           std::string dataFile,
                           std::string pathOut,
                           std::string predictFile,
                           std::string lib,
                           std::string pred,
                           int         D,
                           int         E,
                           int         Tp,
                           int         knn,
                           int         tau,
                           std::string columns,
                           std::string target,
                           int         multiview,
                           int         exclusionRadius,
                           bool        trainLib,
                           bool        excludeTarget,
                           bool        parameterList,
                           bool        verbose,
                           unsigned    nThreads )
{
    // DataFrame constructor loads data
    DataFrame< double > DF( pathIn, dataFile );

    MultiviewValues mvValues = Multiview( std::ref( DF ), pathOut, predictFile,
                                          lib, pred, D, E, Tp, knn, tau,
                                          columns, target, multiview,
                                          exclusionRadius, trainLib,
                                          excludeTarget, parameterList,
                                          verbose, nThreads );

    return mvValues;
}

//----------------------------------------------------------------------
// Multiview with DataFrame input
//----------------------------------------------------------------------
MultiviewValues Multiview( DataFrame< double > & DF,
                           std::string pathOut,
                           std::string predictFile,
                           std::string lib,
                           std::string pred,
                           int         D,
                           int         E,
                           int         Tp,
                           int         knn,
                           int         tau,
                           std::string columns,
                           std::string target,
                           int         multiview,
                           int         exclusionRadius,
                           bool        trainLib,
                           bool        excludeTarget,
                           bool        parameterList,
                           bool        verbose,
                           unsigned    nThreads )
{
    // Note: Method::Simplex & embedded = false
    //       Parameters constructor calls Validate()
    //       If embedded = true: Validate() will set E to number of columns
    //       We need E to pass to PrepareEmbedding() : EmbedData()
    Parameters parameters = Parameters( Method::Multiview,
                                        "",           // pathIn
                                        "",           // dataFile
                                        pathOut,      // 
                                        predictFile,  // 
                                        lib,          // lib_str
                                        pred,         // pred_str
                                        E,            // 
                                        Tp,           // 
                                        knn,          // 
                                        tau,          // 
                                        0,            // theta
                                        exclusionRadius,
                                        columns,      // 
                                        target,       // 
                                        false,        // embedded false
                                        false,        // const_predict
                                        verbose,      // 
                                        std::vector<bool>(), // validLib
                                        0,            // generateSteps
                                        false,        // generateLibrary
                                        parameterList,//
                                        "",           // SmapFile
                                        "",           // blockFile
                                        multiview,    // multiviewEnsemble,
                                        D,            // multiviewD
                                        trainLib,     // multiviewTrainLib
                                        excludeTarget );// multiviewExcludeTarget

    // Instantiate EDM::Simplex::Multiview object
    MultiviewClass MultiviewModel = MultiviewClass( DF, std::ref( parameters ) );

    MultiviewModel.Project( nThreads );

    // MultiviewClass MultiviewModel contians MultiviewValues MVvalues
    MultiviewModel.MVvalues.parameterMap = MultiviewModel.parameters.Map;

    return MultiviewModel.MVvalues;
}
