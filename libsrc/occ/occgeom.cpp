
#ifdef OCCGEOMETRY

#include <mystdlib.h>
#include <occgeom.hpp>
#include <core/register_archive.hpp>
#include <cstdio>
#include "ShapeAnalysis_ShapeTolerance.hxx"
#include "ShapeAnalysis_ShapeContents.hxx"
#include "ShapeAnalysis_CheckSmallFace.hxx"
#include "ShapeAnalysis_DataMapOfShapeListOfReal.hxx"
#include "ShapeAnalysis_Surface.hxx"

#include "BRepCheck_Analyzer.hxx"
#include "BRepLib.hxx"
#include "ShapeBuild_ReShape.hxx"
#include "ShapeFix.hxx"
#include "ShapeFix_FixSmallFace.hxx"
#include "Partition_Spliter.hxx"
#include "BRepAlgoAPI_Fuse.hxx"
#include "Interface_InterfaceModel.hxx"

#include "XSControl_WorkSession.hxx"
#include "XSControl_TransferReader.hxx"
#include "StepRepr_RepresentationItem.hxx"
#include "StepBasic_ProductDefinitionRelationship.hxx"
#include "Transfer_TransientProcess.hxx"
#include "TransferBRep.hxx"
#ifndef _Standard_Version_HeaderFile
#include <Standard_Version.hxx>
#endif

#include <STEPConstruct.hxx>
#include <Transfer_FinderProcess.hxx>
#include <TDataStd_Name.hxx>
#include <XCAFPrs.hxx>
#include <TopoDS_Shape.hxx>
#include <XCAFPrs_Style.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <NCollection_IndexedDataMap.hxx>
#include <StepShape_TopologicalRepresentationItem.hxx>

#if OCC_VERSION_HEX < 0x070000
// pass
#elif OCC_VERSION_HEX < 0x070200
   #include "StlTransfer.hxx"
   #include "TopoDS_Iterator.hxx"
#else
   #include "TopoDS_Iterator.hxx"
#endif

namespace netgen
{
  void LoadOCCInto(OCCGeometry* occgeo, const char* filename);
  void PrintContents (OCCGeometry * geom);

  std::map<Handle(TopoDS_TShape), ShapeProperties> OCCGeometry::global_shape_properties;
  std::map<Handle(TopoDS_TShape), std::vector<OCCIdentification>> OCCGeometry::identifications;
  
  OCCGeometry::OCCGeometry(const TopoDS_Shape& _shape, int aoccdim, bool copy)
  {
    if(copy)
      {
        auto filename = GetTempFilename();
        step_utils::WriteSTEP(_shape, filename.c_str());
        LoadOCCInto(this, filename.c_str());
        occdim = aoccdim;
        std::remove(filename.c_str());
      }
    else
      {
        shape = _shape;
        changed = 1;
        occdim = aoccdim;
        BuildFMap();
        CalcBoundingBox();
        PrintContents (this);
      }
  }

  string STEP_GetEntityName(const TopoDS_Shape & theShape, STEPCAFControl_Reader * aReader)
  {
    const Handle(XSControl_WorkSession)& theSession = aReader->Reader().WS();
    const Handle(XSControl_TransferReader)& aTransferReader =
        theSession->TransferReader();

    Handle(Standard_Transient) anEntity =
        aTransferReader->EntityFromShapeResult(theShape, 1);

    if (anEntity.IsNull()) // as just mapped
        anEntity = aTransferReader->EntityFromShapeResult (theShape,-1);

    if (anEntity.IsNull()) // as anything
        anEntity = aTransferReader->EntityFromShapeResult (theShape,4);

    if (anEntity.IsNull())
      {
        cout<<"Warning: cannot get entity from shape" <<endl;
        return "none";
      }

    auto aReprItem = Handle(StepRepr_RepresentationItem)::DownCast(anEntity);
    if(!aReprItem.IsNull())
        return aReprItem->Name()->ToCString();;

    auto bReprItem = Handle(StepBasic_ProductDefinitionRelationship)::DownCast(anEntity);
    if (!bReprItem.IsNull())
        return bReprItem->Description()->ToCString();

    cout<<"Warning: unknown entity type " << anEntity->DynamicType() << endl;
    return "none";
  }

  void OCCGeometry :: Analyse(Mesh& mesh,
                              const MeshingParameters& mparam) const
  {
    OCCSetLocalMeshSize(*this, mesh, mparam, occparam);
  }

  void OCCGeometry :: FindEdges(Mesh& mesh,
                                const MeshingParameters& mparam) const
  {
    OCCFindEdges(*this, mesh, mparam);
  }

  void OCCGeometry :: MeshSurface(Mesh& mesh,
                                  const MeshingParameters& mparam) const
  {
    OCCMeshSurface(*this, mesh, mparam);
  }

  void OCCGeometry :: FinalizeMesh(Mesh& mesh) const
  {
    for (int i = 0; i < mesh.GetNDomains(); i++)
      if (auto name = sprops[i]->name)
        mesh.SetMaterial (i+1, *name);
  }

   void OCCGeometry :: PrintNrShapes ()
   {
      TopExp_Explorer e;
      int count = 0;
      for (e.Init(shape, TopAbs_COMPSOLID); e.More(); e.Next()) count++;
      cout << "CompSolids: " << count << endl;

      cout << "Solids    : " << somap.Extent() << endl;
      cout << "Shells    : " << shmap.Extent() << endl;
      cout << "Faces     : " << fmap.Extent() << endl;
      cout << "Edges     : " << emap.Extent() << endl;
      cout << "Vertices  : " << vmap.Extent() << endl;
   }




   void PrintContents (OCCGeometry * geom)
   {
      ShapeAnalysis_ShapeContents cont;
      cont.Clear();
      cont.Perform(geom->shape);

      (*testout) << "OCC CONTENTS" << endl;
      (*testout) << "============" << endl;
      (*testout) << "SOLIDS   : " << cont.NbSolids() << endl;
      (*testout) << "SHELLS   : " << cont.NbShells() << endl;
      (*testout) << "FACES    : " << cont.NbFaces() << endl;
      (*testout) << "WIRES    : " << cont.NbWires() << endl;
      (*testout) << "EDGES    : " << cont.NbEdges() << endl;
      (*testout) << "VERTICES : " << cont.NbVertices() << endl;

      TopExp_Explorer e;
      int count = 0;
      for (e.Init(geom->shape, TopAbs_COMPOUND); e.More(); e.Next())
         count++;
      (*testout) << "Compounds: " << count << endl;

      count = 0;
      for (e.Init(geom->shape, TopAbs_COMPSOLID); e.More(); e.Next())
         count++;
      (*testout) << "CompSolids: " << count << endl;

      (*testout) << endl;

      cout << IM(3) << "Highest entry in topology hierarchy: " << endl;
      if (count)
         cout << IM(3) << count << " composite solid(s)" << endl;
      else
         if (geom->somap.Extent())
            cout << IM(3) << geom->somap.Extent() << " solid(s)" << endl;
         else
            if (geom->shmap.Extent())
               cout << IM(3) << geom->shmap.Extent() << " shells(s)" << endl;
            else
               if (geom->fmap.Extent())
                  cout << IM(3) << geom->fmap.Extent() << " face(s)" << endl;
               else
                  if (geom->wmap.Extent())
                     cout << IM(3) << geom->wmap.Extent() << " wire(s)" << endl;
                  else
                     if (geom->emap.Extent())
                        cout << IM(3) << geom->emap.Extent() << " edge(s)" << endl;
                     else
                        if (geom->vmap.Extent())
                           cout << IM(3) << geom->vmap.Extent() << " vertices(s)" << endl;
                        else
                           cout << IM(3) << "no entities" << endl;

   }

  void OCCGeometry :: GlueGeometry()
  {
    PrintMessage(1, "OCC Glue Geometry");
    /*
      // 
    BRep_Builder builder;
    TopoDS_Shape my_fuse;
    int cnt = 0;
    for (TopExp_Explorer exp_solid(shape, TopAbs_SOLID); exp_solid.More(); exp_solid.Next())
      {
        cout << "cnt = " << cnt << endl;
	if (cnt == 0)
	  my_fuse = exp_solid.Current();
	else
          // my_fuse = BRepAlgoAPI_Fuse (my_fuse, exp_solid.Current());
          my_fuse = QANewModTopOpe_Glue::QANewModTopOpe_Glue(my_fuse, exp_solid.Current());
	cnt++;
      }
    cout << "remove" << endl;
    // for (int i = 1; i <= somap.Size(); i++)
    // builder.Remove (shape, somap(i));
    cout << "now add" << endl;
    // builder.Add (shape, my_fuse);
    shape = my_fuse;
    cout << "build fmap" << endl;
    BuildFMap();
    */


    // from 
    // https://www.opencascade.com/doc/occt-7.4.0/overview/html/occt_user_guides__boolean_operations.html
    BOPAlgo_Builder aBuilder;

    // Setting arguments
    TopTools_ListOfShape aLSObjects; 
    for (TopExp_Explorer exp_solid(shape, TopAbs_SOLID); exp_solid.More(); exp_solid.Next())
      aLSObjects.Append (exp_solid.Current());
    aBuilder.SetArguments(aLSObjects);

    // Setting options for GF
    // Set parallel processing mode (default is false)
    // Standard_Boolean bRunParallel = Standard_True;
    // aBuilder.SetRunParallel(bRunParallel);
    
    // Set Fuzzy value (default is Precision::Confusion())
    // Standard_Real aFuzzyValue = 1.e-5;
    // aBuilder.SetFuzzyValue(aFuzzyValue);
    
    // Set safe processing mode (default is false)
    // Standard_Boolean bSafeMode = Standard_True;
    // aBuilder.SetNonDestructive(bSafeMode);
    
    // Set Gluing mode for coinciding arguments (default is off)
    // BOPAlgo_GlueEnum aGlue = BOPAlgo_GlueShift;
    // aBuilder.SetGlue(aGlue);
    
    // Disabling/Enabling the check for inverted solids (default is true)
    // Standard Boolean bCheckInverted = Standard_False;
    // aBuilder.SetCheckInverted(bCheckInverted);
    
    // Set OBB usage (default is false)
    // Standard_Boolean bUseOBB = Standard_True;
    // aBuilder.SetUseOBB(buseobb);
    
    // Perform the operation
    aBuilder.Perform();
    // Check for the errors
#if OCC_VERSION_HEX >= 0x070200
    if (aBuilder.HasErrors())
      {
        cout << "builder has errors" << endl;
        return;
      }
    // Check for the warnings
    if (aBuilder.HasWarnings())
      {
        // treatment of the warnings
        ;
      }
#endif

#ifdef OCC_HAVE_HISTORY    
    Handle(BRepTools_History) history = aBuilder.History ();
    
    for (TopExp_Explorer e(shape, TopAbs_SOLID); e.More(); e.Next())
      {
        if (auto name = OCCGeometry::global_shape_properties[e.Current().TShape()].name)
          for (auto mods : history->Modified(e.Current()))
            OCCGeometry::global_shape_properties[mods.TShape()].name = *name;
      }
#endif // OCC_HAVE_HISTORY    
    
    // result of the operation
    shape = aBuilder.Shape();
    BuildFMap();
  }

   void OCCGeometry :: HealGeometry ()
   {
      int nrc = 0, nrcs = 0,
         nrso = somap.Extent(),
         nrsh = shmap.Extent(),
         nrf = fmap.Extent(),
         nrw = wmap.Extent(),
         nre = emap.Extent(),
         nrv = vmap.Extent();

      TopExp_Explorer exp0;
      TopExp_Explorer exp1;


      for (exp0.Init(shape, TopAbs_COMPOUND); exp0.More(); exp0.Next()) nrc++;
      for (exp0.Init(shape, TopAbs_COMPSOLID); exp0.More(); exp0.Next()) nrcs++;

      double surfacecont = 0;
      
      {
         Handle(ShapeBuild_ReShape) rebuild = new ShapeBuild_ReShape;
         rebuild->Apply(shape);
         for (exp1.Init (shape, TopAbs_EDGE); exp1.More(); exp1.Next())
         {
            TopoDS_Edge edge = TopoDS::Edge(exp1.Current());
            if ( BRep_Tool::Degenerated(edge) )
               rebuild->Remove(edge);
         }
         shape = rebuild->Apply(shape);
      }

      BuildFMap();


      for (exp0.Init (shape, TopAbs_FACE); exp0.More(); exp0.Next())
      {
         TopoDS_Face face = TopoDS::Face(exp0.Current());

         GProp_GProps system;
         BRepGProp::SurfaceProperties(face, system);
         surfacecont += system.Mass();
      }


      cout << "Starting geometry healing procedure (tolerance: " << tolerance << ")" << endl
         << "-----------------------------------" << endl;

      {
         cout << endl << "- repairing faces" << endl;

         Handle(ShapeFix_Face) sff;
         Handle(ShapeBuild_ReShape) rebuild = new ShapeBuild_ReShape;
         rebuild->Apply(shape);

         for (exp0.Init (shape, TopAbs_FACE); exp0.More(); exp0.Next())
         {
            TopoDS_Face face = TopoDS::Face (exp0.Current());
            auto props = global_shape_properties[face.TShape()];

            sff = new ShapeFix_Face (face);
            sff->FixAddNaturalBoundMode() = Standard_True;
            sff->FixSmallAreaWireMode() = Standard_True;
            sff->Perform();

            if(sff->Status(ShapeExtend_DONE1) ||
               sff->Status(ShapeExtend_DONE2) ||
               sff->Status(ShapeExtend_DONE3) ||
               sff->Status(ShapeExtend_DONE4) ||
               sff->Status(ShapeExtend_DONE5))
            {
               cout << "repaired face " << fmap.FindIndex(face) << " ";
               if(sff->Status(ShapeExtend_DONE1))
                  cout << "(some wires are fixed)" <<endl;
               else if(sff->Status(ShapeExtend_DONE2))
                  cout << "(orientation of wires fixed)" <<endl;
               else if(sff->Status(ShapeExtend_DONE3))
                  cout << "(missing seam added)" <<endl;
               else if(sff->Status(ShapeExtend_DONE4))
                  cout << "(small area wire removed)" <<endl;
               else if(sff->Status(ShapeExtend_DONE5))
                  cout << "(natural bounds added)" <<endl;
               TopoDS_Face newface = sff->Face();

               rebuild->Replace(face, newface);
            }

            // Set the original properties of the face to the newly created 
            // face (after the healing process)
            global_shape_properties[face.TShape()];
         }
         shape = rebuild->Apply(shape);
      }


      {
         Handle(ShapeBuild_ReShape) rebuild = new ShapeBuild_ReShape;
         rebuild->Apply(shape);
         for (exp1.Init (shape, TopAbs_EDGE); exp1.More(); exp1.Next())
         {
            TopoDS_Edge edge = TopoDS::Edge(exp1.Current());
            if ( BRep_Tool::Degenerated(edge) )
               rebuild->Remove(edge);
         }
         shape = rebuild->Apply(shape);
      }


      if (fixsmalledges)
      {
         cout << endl << "- fixing small edges" << endl;

         Handle(ShapeFix_Wire) sfw;
         Handle(ShapeBuild_ReShape) rebuild = new ShapeBuild_ReShape;
         rebuild->Apply(shape);


         for (exp0.Init (shape, TopAbs_FACE); exp0.More(); exp0.Next())
         {
            TopoDS_Face face = TopoDS::Face(exp0.Current());

            for (exp1.Init (face, TopAbs_WIRE); exp1.More(); exp1.Next())
            {
               TopoDS_Wire oldwire = TopoDS::Wire(exp1.Current());
               sfw = new ShapeFix_Wire (oldwire, face ,tolerance);
               sfw->ModifyTopologyMode() = Standard_True;

               sfw->ClosedWireMode() = Standard_True;

               bool replace = false;

               replace = sfw->FixReorder() || replace;

               replace = sfw->FixConnected() || replace;



               if (sfw->FixSmall (Standard_False, tolerance) && ! (sfw->StatusSmall(ShapeExtend_FAIL1) ||
                  sfw->StatusSmall(ShapeExtend_FAIL2) ||
                  sfw->StatusSmall(ShapeExtend_FAIL3)))
               {
                  cout << "Fixed small edge in wire " << wmap.FindIndex (oldwire) << endl;
                  replace = true;

               }
               else if (sfw->StatusSmall(ShapeExtend_FAIL1))
                  cerr << "Failed to fix small edge in wire " << wmap.FindIndex (oldwire)
                  << ", edge cannot be checked (no 3d curve and no pcurve)" << endl;
               else if (sfw->StatusSmall(ShapeExtend_FAIL2))
                  cerr << "Failed to fix small edge in wire " << wmap.FindIndex (oldwire)
                  << ", edge is null-length and has different vertives at begin and end, and lockvtx is True or ModifiyTopologyMode is False" << endl;
               else if (sfw->StatusSmall(ShapeExtend_FAIL3))
                  cerr << "Failed to fix small edge in wire " << wmap.FindIndex (oldwire)
                  << ", CheckConnected has failed" << endl;

               replace = sfw->FixEdgeCurves() || replace;

               replace = sfw->FixDegenerated() || replace;

               replace = sfw->FixSelfIntersection() || replace;

               replace = sfw->FixLacking(Standard_True) || replace;

               if(replace)
               {
                  TopoDS_Wire newwire = sfw->Wire();
                  rebuild->Replace(oldwire, newwire);
               }

               //delete sfw; sfw = NULL;

            }
         }

         shape = rebuild->Apply(shape);



         {
            BuildFMap();
            Handle(ShapeBuild_ReShape) rebuild = new ShapeBuild_ReShape;
            rebuild->Apply(shape);

            for (exp1.Init (shape, TopAbs_EDGE); exp1.More(); exp1.Next())
            {
               TopoDS_Edge edge = TopoDS::Edge(exp1.Current());
               if (vmap.FindIndex(TopExp::FirstVertex (edge)) ==
                  vmap.FindIndex(TopExp::LastVertex (edge)))
               {
                  GProp_GProps system;
                  BRepGProp::LinearProperties(edge, system);
                  if (system.Mass() < tolerance)
                  {
                     cout << "removing degenerated edge " << emap.FindIndex(edge)
                        << " from vertex " << vmap.FindIndex(TopExp::FirstVertex (edge))
                        << " to vertex " << vmap.FindIndex(TopExp::LastVertex (edge)) << endl;
                     rebuild->Remove(edge);
                  }
               }
            }
            shape = rebuild->Apply(shape);

            //delete rebuild; rebuild = NULL;
         }



         {
            Handle(ShapeBuild_ReShape) rebuild = new ShapeBuild_ReShape;
            rebuild->Apply(shape);
            for (exp1.Init (shape, TopAbs_EDGE); exp1.More(); exp1.Next())
            {
               TopoDS_Edge edge = TopoDS::Edge(exp1.Current());
               if ( BRep_Tool::Degenerated(edge) )
                  rebuild->Remove(edge);
            }
            shape = rebuild->Apply(shape);
         }




         Handle(ShapeFix_Wireframe) sfwf = new ShapeFix_Wireframe;
         sfwf->SetPrecision(tolerance);
         sfwf->Load (shape);
         sfwf->ModeDropSmallEdges() = Standard_True;

         sfwf->SetPrecision(boundingbox.Diam());

         if (sfwf->FixWireGaps())
         {
            cout << endl << "- fixing wire gaps" << endl;
            if (sfwf->StatusWireGaps(ShapeExtend_OK)) cout << "no gaps found" << endl;
            if (sfwf->StatusWireGaps(ShapeExtend_DONE1)) cout << "some 2D gaps fixed" << endl;
            if (sfwf->StatusWireGaps(ShapeExtend_DONE2)) cout << "some 3D gaps fixed" << endl;
            if (sfwf->StatusWireGaps(ShapeExtend_FAIL1)) cout << "failed to fix some 2D gaps" << endl;
            if (sfwf->StatusWireGaps(ShapeExtend_FAIL2)) cout << "failed to fix some 3D gaps" << endl;
         }

         sfwf->SetPrecision(tolerance);


         {
            for (exp1.Init (shape, TopAbs_EDGE); exp1.More(); exp1.Next())
            {
               TopoDS_Edge edge = TopoDS::Edge(exp1.Current());
               if ( BRep_Tool::Degenerated(edge) )
                  cout << "degenerated edge at position 4" << endl;
            }
         }



         if (sfwf->FixSmallEdges())
         {
            cout << endl << "- fixing wire frames" << endl;
            if (sfwf->StatusSmallEdges(ShapeExtend_OK)) cout << "no small edges found" << endl;
            if (sfwf->StatusSmallEdges(ShapeExtend_DONE1)) cout << "some small edges fixed" << endl;
            if (sfwf->StatusSmallEdges(ShapeExtend_FAIL1)) cout << "failed to fix some small edges" << endl;
         }



         shape = sfwf->Shape();

         //delete sfwf; sfwf = NULL;
         //delete rebuild; rebuild = NULL;

      }





      {
         for (exp1.Init (shape, TopAbs_EDGE); exp1.More(); exp1.Next())
         {
            TopoDS_Edge edge = TopoDS::Edge(exp1.Current());
            if ( BRep_Tool::Degenerated(edge) )
               cout << "degenerated edge at position 5" << endl;
         }
      }




      if (fixspotstripfaces)
      {

         cout << endl << "- fixing spot and strip faces" << endl;
         Handle(ShapeFix_FixSmallFace) sffsm = new ShapeFix_FixSmallFace();
         sffsm -> Init (shape);
         sffsm -> SetPrecision (tolerance);
         sffsm -> Perform();

         shape = sffsm -> FixShape();
         //delete sffsm; sffsm = NULL;
      }


      {
         for (exp1.Init (shape, TopAbs_EDGE); exp1.More(); exp1.Next())
         {
            TopoDS_Edge edge = TopoDS::Edge(exp1.Current());
            if ( BRep_Tool::Degenerated(edge) )
               cout << "degenerated edge at position 6" << endl;
         }
      }



      if (sewfaces)
      {
         cout << endl << "- sewing faces" << endl;

         BRepOffsetAPI_Sewing sewedObj(tolerance);

         for (exp0.Init (shape, TopAbs_FACE); exp0.More(); exp0.Next())
         {
            TopoDS_Face face = TopoDS::Face (exp0.Current());
            sewedObj.Add (face);
         }

         sewedObj.Perform();

         if (!sewedObj.SewedShape().IsNull())
            shape = sewedObj.SewedShape();
         else
            cout << " not possible";
      }



      {
         Handle(ShapeBuild_ReShape) rebuild = new ShapeBuild_ReShape;
         rebuild->Apply(shape);
         for (exp1.Init (shape, TopAbs_EDGE); exp1.More(); exp1.Next())
         {
            TopoDS_Edge edge = TopoDS::Edge(exp1.Current());
            if ( BRep_Tool::Degenerated(edge) )
               rebuild->Remove(edge);
         }
         shape = rebuild->Apply(shape);
      }


      if (makesolids)
      {
         cout << endl << "- making solids" << endl;

         BRepBuilderAPI_MakeSolid ms;
         int count = 0;
         for (exp0.Init(shape, TopAbs_SHELL); exp0.More(); exp0.Next())
         {
            count++;
            ms.Add (TopoDS::Shell(exp0.Current()));
         }

         if (!count)
         {
            cout << " not possible (no shells)" << endl;
         }
         else
         {
            BRepCheck_Analyzer ba(ms);
            if (ba.IsValid ())
            {
               Handle(ShapeFix_Shape) sfs = new ShapeFix_Shape;
               sfs->Init (ms);
               sfs->SetPrecision(tolerance);
               sfs->SetMaxTolerance(tolerance);
               sfs->Perform();
               shape = sfs->Shape();

               for (exp0.Init(shape, TopAbs_SOLID); exp0.More(); exp0.Next())
               {
                  TopoDS_Solid solid = TopoDS::Solid(exp0.Current());
                  TopoDS_Solid newsolid = solid;
                  BRepLib::OrientClosedSolid (newsolid);
                  Handle(ShapeBuild_ReShape) rebuild = new ShapeBuild_ReShape;
                  //		  rebuild->Apply(shape);
                  rebuild->Replace(solid, newsolid);
                  TopoDS_Shape newshape = rebuild->Apply(shape, TopAbs_COMPSOLID);//, 1);
                  //		  TopoDS_Shape newshape = rebuild->Apply(shape);
                  shape = newshape;
               }

               //delete sfs; sfs = NULL;
            }
            else
               cout << " not possible" << endl;
         }
      }



      if (splitpartitions)
      {
         cout << "- running SALOME partition splitter" << endl;

         TopExp_Explorer e2;
         Partition_Spliter ps;
         int count = 0;

         for (e2.Init (shape, TopAbs_SOLID);
            e2.More(); e2.Next())
         {
            count++;
            ps.AddShape (e2.Current());
         }

         ps.Compute();
         shape = ps.Shape();

         cout << " before: " << count << " solids" << endl;

         count = 0;
         for (e2.Init (shape, TopAbs_SOLID);
            e2.More(); e2.Next()) count++;

            cout << " after : " << count << " solids" << endl;
      }

      BuildFMap();



      {
         for (exp1.Init (shape, TopAbs_EDGE); exp1.More(); exp1.Next())
         {
            TopoDS_Edge edge = TopoDS::Edge(exp1.Current());
            if ( BRep_Tool::Degenerated(edge) )
               cout << "degenerated edge at position 8" << endl;
         }
      }


      double newsurfacecont = 0;


      for (exp0.Init (shape, TopAbs_FACE); exp0.More(); exp0.Next())
      {
         TopoDS_Face face = TopoDS::Face(exp0.Current());
         GProp_GProps system;
         BRepGProp::SurfaceProperties(face, system);
         newsurfacecont += system.Mass();
      }


      int nnrc = 0, nnrcs = 0,
         nnrso = somap.Extent(),
         nnrsh = shmap.Extent(),
         nnrf = fmap.Extent(),
         nnrw = wmap.Extent(),
         nnre = emap.Extent(),
         nnrv = vmap.Extent();

      for (exp0.Init(shape, TopAbs_COMPOUND); exp0.More(); exp0.Next()) nnrc++;
      for (exp0.Init(shape, TopAbs_COMPSOLID); exp0.More(); exp0.Next()) nnrcs++;

      cout << "-----------------------------------" << endl;
      cout << "Compounds       : " << nnrc << " (" << nrc << ")" << endl;
      cout << "Composite solids: " << nnrcs << " (" << nrcs << ")" << endl;
      cout << "Solids          : " << nnrso << " (" << nrso << ")" << endl;
      cout << "Shells          : " << nnrsh << " (" << nrsh << ")" << endl;
      cout << "Wires           : " << nnrw << " (" << nrw << ")" << endl;
      cout << "Faces           : " << nnrf << " (" << nrf << ")" << endl;
      cout << "Edges           : " << nnre << " (" << nre << ")" << endl;
      cout << "Vertices        : " << nnrv << " (" << nrv << ")" << endl;
      cout << endl;
      cout << "Totol surface area : " << newsurfacecont << " (" << surfacecont << ")" << endl;
      cout << endl;
   }



   void OCCGeometry :: BuildFMap()
   {
      somap.Clear();
      shmap.Clear();
      fmap.Clear();
      wmap.Clear();
      emap.Clear();
      vmap.Clear();

      TopExp_Explorer exp0, exp1, exp2, exp3, exp4, exp5;

      for (exp0.Init(shape, TopAbs_COMPOUND);
         exp0.More(); exp0.Next())
      {
         TopoDS_Compound compound = TopoDS::Compound (exp0.Current());
         (*testout) << "compound" << endl;
         int i = 0;
         for (exp1.Init(compound, TopAbs_SHELL);
            exp1.More(); exp1.Next())
         {
            (*testout) << "shell " << ++i << endl;
         }
      }

      for (exp0.Init(shape, TopAbs_SOLID);
         exp0.More(); exp0.Next())
      {
         TopoDS_Solid solid = TopoDS::Solid (exp0.Current());

         if (somap.FindIndex(solid) < 1)
         {
            somap.Add (solid);

            for (exp1.Init(solid, TopAbs_SHELL);
               exp1.More(); exp1.Next())
            {
               TopoDS_Shell shell = TopoDS::Shell (exp1.Current());
               if (shmap.FindIndex(shell) < 1)
               {
                  shmap.Add (shell);

                  for (exp2.Init(shell, TopAbs_FACE);
                     exp2.More(); exp2.Next())
                  {
                     TopoDS_Face face = TopoDS::Face(exp2.Current());
                     if (fmap.FindIndex(face) < 1)
                     {
                        fmap.Add (face);
                        (*testout) << "face " << fmap.FindIndex(face) << " ";
                        (*testout) << ((face.Orientation() == TopAbs_REVERSED) ? "-" : "+") << ", ";
                        (*testout) << ((exp2.Current().Orientation() == TopAbs_REVERSED) ? "-" : "+") << endl;
                        for (exp3.Init(exp2.Current(), TopAbs_WIRE);
                           exp3.More(); exp3.Next())
                        {
                           TopoDS_Wire wire = TopoDS::Wire (exp3.Current());
                           if (wmap.FindIndex(wire) < 1)
                           {
                              wmap.Add (wire);

                              for (exp4.Init(exp3.Current(), TopAbs_EDGE);
                                 exp4.More(); exp4.Next())
                              {
                                 TopoDS_Edge edge = TopoDS::Edge(exp4.Current());
                                 if (emap.FindIndex(edge) < 1)
                                 {
                                    emap.Add (edge);
                                    for (exp5.Init(exp4.Current(), TopAbs_VERTEX);
                                       exp5.More(); exp5.Next())
                                    {
                                       TopoDS_Vertex vertex = TopoDS::Vertex(exp5.Current());
                                       if (vmap.FindIndex(vertex) < 1)
                                          vmap.Add (vertex);
                                    }
                                 }
                              }
                           }
                        }
                     }
                  }
               }
            }
         }
      }

      // Free Shells
      for (exp1.Init(shape, TopAbs_SHELL, TopAbs_SOLID); exp1.More(); exp1.Next())
      {
         TopoDS_Shell shell = TopoDS::Shell(exp1.Current());
         if (shmap.FindIndex(shell) < 1)
         {
            shmap.Add (shell);

            (*testout) << "shell " << shmap.FindIndex(shell) << " ";
            (*testout) << ((shell.Orientation() == TopAbs_REVERSED) ? "-" : "+") << ", ";
            (*testout) << ((exp1.Current().Orientation() == TopAbs_REVERSED) ? "-" : "+") << endl;

            for (exp2.Init(shell, TopAbs_FACE); exp2.More(); exp2.Next())
            {
               TopoDS_Face face = TopoDS::Face(exp2.Current());
               if (fmap.FindIndex(face) < 1)
               {
                 fmap.Add (face);

                  for (exp3.Init(face, TopAbs_WIRE); exp3.More(); exp3.Next())
                  {
                     TopoDS_Wire wire = TopoDS::Wire (exp3.Current());
                     if (wmap.FindIndex(wire) < 1)
                     {
                        wmap.Add (wire);

                        for (exp4.Init(wire, TopAbs_EDGE); exp4.More(); exp4.Next())
                        {
                           TopoDS_Edge edge = TopoDS::Edge(exp4.Current());
                           if (emap.FindIndex(edge) < 1)
                           {
                              emap.Add (edge);
                              for (exp5.Init(edge, TopAbs_VERTEX); exp5.More(); exp5.Next())
                              {
                                 TopoDS_Vertex vertex = TopoDS::Vertex(exp5.Current());
                                 if (vmap.FindIndex(vertex) < 1)
                                    vmap.Add (vertex);
                              }
                           }
                        }
                     }
                  }
               }
            }
         }
      }


      // Free Faces
      for (auto face : MyExplorer(shape, TopAbs_FACE, TopAbs_SHELL))
        if (!fmap.Contains(face))
          {
            fmap.Add (face);
            for (auto wire : MyExplorer(face, TopAbs_WIRE))
              if (!wmap.Contains(wire))
                {
                  wmap.Add (wire);
                  for (auto edge : MyExplorer(wire, TopAbs_EDGE))
                    if (!emap.Contains(edge))
                      {
                        emap.Add (edge);
                        for (auto vertex : MyExplorer(edge, TopAbs_VERTEX))
                          if (!vmap.Contains(vertex))
                            vmap.Add (vertex);
                      }
                }
          }


      // Free Wires

      for (exp3.Init(shape, TopAbs_WIRE, TopAbs_FACE); exp3.More(); exp3.Next())
      {
         TopoDS_Wire wire = TopoDS::Wire (exp3.Current());
         if (wmap.FindIndex(wire) < 1)
         {
            wmap.Add (wire);

            for (exp4.Init(exp3.Current(), TopAbs_EDGE); exp4.More(); exp4.Next())
            {
               TopoDS_Edge edge = TopoDS::Edge(exp4.Current());
               if (emap.FindIndex(edge) < 1)
               {
                  emap.Add (edge);
                  for (exp5.Init(exp4.Current(), TopAbs_VERTEX); exp5.More(); exp5.Next())
                  {
                     TopoDS_Vertex vertex = TopoDS::Vertex(exp5.Current());
                     if (vmap.FindIndex(vertex) < 1)
                        vmap.Add (vertex);
                  }
               }
            }
         }
      }


      // Free Edges
      /*
      for (exp4.Init(shape, TopAbs_EDGE, TopAbs_WIRE); exp4.More(); exp4.Next())
      {
         TopoDS_Edge edge = TopoDS::Edge(exp4.Current());
         if (emap.FindIndex(edge) < 1)
         {
            emap.Add (edge);
            for (exp5.Init(exp4.Current(), TopAbs_VERTEX); exp5.More(); exp5.Next())
            {
               TopoDS_Vertex vertex = TopoDS::Vertex(exp5.Current());
               if (vmap.FindIndex(vertex) < 1)
                  vmap.Add (vertex);
            }
         }
      }
      */
      for (auto edge : MyExplorer(shape, TopAbs_EDGE, TopAbs_WIRE))
        if (!emap.Contains(edge))
          {
            emap.Add (edge);
            for (auto vertex : MyExplorer(edge, TopAbs_VERTEX))
              if (!vmap.Contains(vertex))
                vmap.Add (vertex);
          }

      
      // Free Vertices
      /*
      for (exp5.Init(shape, TopAbs_VERTEX, TopAbs_EDGE); exp5.More(); exp5.Next())
      {
         TopoDS_Vertex vertex = TopoDS::Vertex(exp5.Current());
         if (vmap.FindIndex(vertex) < 1)
            vmap.Add (vertex);
      }
      */
      for (auto vertex : MyExplorer(shape, TopAbs_VERTEX, TopAbs_EDGE))
        if (!vmap.Contains(vertex))
          vmap.Add (vertex);

      facemeshstatus.DeleteAll();
      facemeshstatus.SetSize (fmap.Extent());
      facemeshstatus = 0;

      // Philippose - 15/01/2009
      face_maxh.DeleteAll();
      face_maxh.SetSize (fmap.Extent());
      face_maxh = 1e99; // mparam.maxh;

      // Philippose - 15/01/2010      
      face_maxh_modified.DeleteAll();      
      face_maxh_modified.SetSize(fmap.Extent());      
      face_maxh_modified = 0;
      

      // Philippose - 17/01/2009
      face_sel_status.DeleteAll();
      face_sel_status.SetSize (fmap.Extent());
      face_sel_status = 0;

      fvispar.SetSize (fmap.Extent());
      evispar.SetSize (emap.Extent());
      vvispar.SetSize (vmap.Extent());

      fsingular.SetSize (fmap.Extent());
      esingular.SetSize (emap.Extent());
      vsingular.SetSize (vmap.Extent());

      fsingular = esingular = vsingular = false;


      sprops.SetSize(somap.Extent());
      for (TopExp_Explorer e(shape, TopAbs_SOLID); e.More(); e.Next())
      {
          auto s = e.Current();
          sprops[somap.FindIndex(s)-1] = &global_shape_properties[s.TShape()];
      }

      fprops.SetSize(fmap.Extent());
      for (TopExp_Explorer e(shape, TopAbs_FACE); e.More(); e.Next())
      {
          auto s = e.Current();
          fprops[fmap.FindIndex(s)-1] = &global_shape_properties[s.TShape()];
      }

      eprops.SetSize(emap.Extent());
      /*
      for (TopExp_Explorer e(shape, TopAbs_EDGE); e.More(); e.Next())
      {
          auto s = e.Current();
          eprops[emap.FindIndex(s)-1] = &global_shape_properties[s.TShape()];
      }
      */
      for (auto [nr,s] : Enumerate(emap))
        eprops[nr-1] = &global_shape_properties[s.TShape()];
   }



   void OCCGeometry :: SewFaces ()
   {
      (*testout) << "Trying to sew faces ..." << endl;
      cout << "Trying to sew faces ..." << flush;

      BRepOffsetAPI_Sewing sewedObj(1);
 
      for (int i = 1; i <= fmap.Extent(); i++)
      {
         TopoDS_Face face = TopoDS::Face (fmap(i));
         sewedObj.Add (face);
      }

      sewedObj.Perform();

      if (!sewedObj.SewedShape().IsNull())
      {
         shape = sewedObj.SewedShape();
         cout << " done" << endl;
      }
      else
         cout << " not possible";
   }





   void OCCGeometry :: MakeSolid ()
   {
      TopExp_Explorer exp0;

      (*testout) << "Trying to build solids ..." << endl;
      cout << "Trying to build solids ..." << flush;

      BRepBuilderAPI_MakeSolid ms;
      int count = 0;
      for (exp0.Init(shape, TopAbs_SHELL); exp0.More(); exp0.Next())
      {
         count++;
         ms.Add (TopoDS::Shell(exp0.Current()));
      }

      if (!count)
      {
         cout << " not possible (no shells)" << endl;
         return;
      }

      BRepCheck_Analyzer ba(ms);
      if (ba.IsValid ())
      {
         Handle(ShapeFix_Shape) sfs = new ShapeFix_Shape;
         sfs->Init (ms);

         sfs->SetPrecision(1e-5);
         sfs->SetMaxTolerance(1e-5);

         sfs->Perform();

         shape = sfs->Shape();

         for (exp0.Init(shape, TopAbs_SOLID); exp0.More(); exp0.Next())
         {
            TopoDS_Solid solid = TopoDS::Solid(exp0.Current());
            TopoDS_Solid newsolid = solid;
            BRepLib::OrientClosedSolid (newsolid);
            Handle(ShapeBuild_ReShape) rebuild = new ShapeBuild_ReShape;
            rebuild->Replace(solid, newsolid);

            TopoDS_Shape newshape = rebuild->Apply(shape, TopAbs_SHAPE, 1);
            shape = newshape;
         }

         cout << " done" << endl;
      }
      else
         cout << " not possible" << endl;
   }




   void OCCGeometry :: BuildVisualizationMesh (double deflection)
   {
      cout << "Preparing visualization (deflection = " << deflection << ") ... " << flush;

      BRepTools::Clean (shape);
      // BRepMesh_IncrementalMesh::
      BRepMesh_IncrementalMesh (shape, deflection, true);
      cout << "done" << endl;
   }




   void OCCGeometry :: CalcBoundingBox ()
   {
      Bnd_Box bb;
#if OCC_VERSION_HEX < 0x070000
      BRepBndLib::Add (shape, bb);
#else
      BRepBndLib::Add ((const TopoDS_Shape) shape, bb,(Standard_Boolean)true);
#endif

      double x1,y1,z1,x2,y2,z2;
      bb.Get (x1,y1,z1,x2,y2,z2);
      Point<3> p1 = Point<3> (x1,y1,z1);
      Point<3> p2 = Point<3> (x2,y2,z2);

      (*testout) << "Bounding Box = [" << p1 << " - " << p2 << "]" << endl;
      boundingbox = Box<3> (p1,p2);
      SetCenter();
   }

   PointGeomInfo OCCGeometry :: ProjectPoint(int surfi, Point<3> & p) const
   {
      static int cnt = 0;
      if (++cnt % 1000 == 0) cout << "Project cnt = " << cnt << endl;

      gp_Pnt pnt(p(0), p(1), p(2));

      double u,v;
      Handle( Geom_Surface ) thesurf = BRep_Tool::Surface(TopoDS::Face(fmap(surfi)));
      Handle( ShapeAnalysis_Surface ) su = new ShapeAnalysis_Surface( thesurf );
      gp_Pnt2d suval = su->ValueOfUV ( pnt, BRep_Tool::Tolerance( TopoDS::Face(fmap(surfi)) ) );
      suval.Coord( u, v);
      pnt = thesurf->Value( u, v );

      PointGeomInfo gi;
      gi.trignum = surfi;
      gi.u = u;
      gi.v = v;
      p = Point<3> (pnt.X(), pnt.Y(), pnt.Z());
      return gi;
   }

  bool OCCGeometry :: ProjectPointGI(int surfind, Point<3>& p, PointGeomInfo& gi) const
  {
    double u = gi.u;
    double v = gi.v;

    Point<3> hp = p;
    if (FastProject (surfind, hp, u, v))
      {
	p = hp;
	return 1;
      }
    ProjectPoint (surfind, p);
    return CalcPointGeomInfo (surfind, gi, p);
  }

  void OCCGeometry :: ProjectPointEdge(int surfind, INDEX surfind2,
                                       Point<3> & p, EdgePointGeomInfo* gi) const
  {
    TopExp_Explorer exp0, exp1;
    bool done = false;
    Handle(Geom_Curve) c;

    for (exp0.Init(fmap(surfind), TopAbs_EDGE); !done && exp0.More(); exp0.Next())
      for (exp1.Init(fmap(surfind2), TopAbs_EDGE); !done && exp1.More(); exp1.Next())
	{
	  if (TopoDS::Edge(exp0.Current()).IsSame(TopoDS::Edge(exp1.Current())))
	    {
	      done = true;
	      double s0, s1;
	      c = BRep_Tool::Curve(TopoDS::Edge(exp0.Current()), s0, s1);
	    }
	}

    gp_Pnt pnt(p(0), p(1), p(2));
    GeomAPI_ProjectPointOnCurve proj(pnt, c);
    pnt = proj.NearestPoint();
    p(0) = pnt.X();
    p(1) = pnt.Y();
    p(2) = pnt.Z();

  }

   bool OCCGeometry :: FastProject (int surfi, Point<3> & ap, double& u, double& v) const
   {
      gp_Pnt p(ap(0), ap(1), ap(2));

      Handle(Geom_Surface) surface = BRep_Tool::Surface(TopoDS::Face(fmap(surfi)));

      gp_Pnt x = surface->Value (u,v);

      if (p.SquareDistance(x) <= sqr(PROJECTION_TOLERANCE)) return true;

      gp_Vec du, dv;

      surface->D1(u,v,x,du,dv);

      int count = 0;

      gp_Pnt xold;
      gp_Vec n;
      double det, lambda, mu;

      do {
         count++;

         n = du^dv;

         det = Det3 (n.X(), du.X(), dv.X(),
            n.Y(), du.Y(), dv.Y(),
            n.Z(), du.Z(), dv.Z());

         if (det < 1e-15) return false;

         lambda = Det3 (n.X(), p.X()-x.X(), dv.X(),
            n.Y(), p.Y()-x.Y(), dv.Y(),
            n.Z(), p.Z()-x.Z(), dv.Z())/det;

         mu     = Det3 (n.X(), du.X(), p.X()-x.X(),
            n.Y(), du.Y(), p.Y()-x.Y(),
            n.Z(), du.Z(), p.Z()-x.Z())/det;

         u += lambda;
         v += mu;

         xold = x;
         surface->D1(u,v,x,du,dv);

      } while (xold.SquareDistance(x) > sqr(PROJECTION_TOLERANCE) && count < 50);

      //    (*testout) << "FastProject count: " << count << endl;

      if (count == 50) return false;

      ap = Point<3> (x.X(), x.Y(), x.Z());

      return true;
   }

  Vec<3> OCCGeometry :: GetNormal(int surfind, const Point<3> & p, const PointGeomInfo* geominfo) const
  {
    if(geominfo)
      {
        gp_Pnt pnt;
        gp_Vec du, dv;

        Handle(Geom_Surface) occface;
        occface = BRep_Tool::Surface(TopoDS::Face(fmap(surfind)));

        occface->D1(geominfo->u,geominfo->v,pnt,du,dv);

        auto n = Cross (Vec<3>(du.X(), du.Y(), du.Z()),
                        Vec<3>(dv.X(), dv.Y(), dv.Z()));
        n.Normalize();

        if (fmap(surfind).Orientation() == TopAbs_REVERSED) n *= -1;
        return n;
      }
    Standard_Real u,v;

    gp_Pnt pnt(p(0), p(1), p(2));

    Handle(Geom_Surface) occface;
    occface = BRep_Tool::Surface(TopoDS::Face(fmap(surfind)));

    /*
    GeomAPI_ProjectPointOnSurf proj(pnt, occface);

    if (proj.NbPoints() < 1)
      {
	cout << "ERROR: OCCSurface :: GetNormalVector: GeomAPI_ProjectPointOnSurf failed!"
	     << endl;
	cout << p << endl;
	return;
      }
 
    proj.LowerDistanceParameters (u, v);
    */
    
    Handle( ShapeAnalysis_Surface ) su = new ShapeAnalysis_Surface( occface );
    gp_Pnt2d suval = su->ValueOfUV ( pnt, BRep_Tool::Tolerance( TopoDS::Face(fmap(surfind)) ) );
    suval.Coord( u, v);
    pnt = occface->Value( u, v );

    gp_Vec du, dv;
    occface->D1(u,v,pnt,du,dv);

    /*
      if (!occface->IsCNu (1) || !occface->IsCNv (1))
      (*testout) << "SurfOpt: Differentiation FAIL" << endl;
    */

    auto n = Cross (Vec3d(du.X(), du.Y(), du.Z()),
	       Vec3d(dv.X(), dv.Y(), dv.Z()));
    n.Normalize();

    if (fmap(surfind).Orientation() == TopAbs_REVERSED) n *= -1;
    return n;
  }

  bool OCCGeometry :: CalcPointGeomInfo(int surfind, PointGeomInfo& gi, const Point<3> & p) const
  {
    Standard_Real u,v;

    gp_Pnt pnt(p(0), p(1), p(2));

    Handle(Geom_Surface) occface;
    occface = BRep_Tool::Surface(TopoDS::Face(fmap(surfind)));

    /*
    GeomAPI_ProjectPointOnSurf proj(pnt, occface);

    if (proj.NbPoints() < 1)
      {
	cout << "ERROR: OCCSurface :: GetNormalVector: GeomAPI_ProjectPointOnSurf failed!"
	     << endl;
	cout << p << endl;
	return 0;
      }
 
    proj.LowerDistanceParameters (u, v);  
    */

    Handle( ShapeAnalysis_Surface ) su = new ShapeAnalysis_Surface( occface );
    gp_Pnt2d suval = su->ValueOfUV ( pnt, BRep_Tool::Tolerance( TopoDS::Face(fmap(surfind)) ) );
    suval.Coord( u, v);
    //pnt = occface->Value( u, v );
    

    gi.u = u;
    gi.v = v;
    return true;
  }

  void OCCGeometry :: PointBetween(const Point<3> & p1, const Point<3> & p2, double secpoint,
                                   int surfi, 
                                   const PointGeomInfo & gi1, 
                                   const PointGeomInfo & gi2,
                                   Point<3> & newp, PointGeomInfo & newgi) const
  {
    Point<3> hnewp;
    hnewp = p1+secpoint*(p2-p1);

    if (surfi > 0)
      {
	double u = gi1.u+secpoint*(gi2.u-gi1.u);
	double v = gi1.v+secpoint*(gi2.v-gi1.v);

        auto savept = hnewp;
	if (!FastProject(surfi, hnewp, u, v) || Dist(hnewp, savept) > Dist(p1,p2))
	  {
            //  cout << "Fast projection to surface fails! Using OCC projection" << endl;
            hnewp = savept;
	    ProjectPoint(surfi, hnewp);
	  }
	newgi.trignum = 1;
        newgi.u = u;
        newgi.v = v;
      }
    newp = hnewp;
  }


  void OCCGeometry :: PointBetweenEdge(const Point<3> & p1,
                                       const Point<3> & p2, double secpoint,
                                       int surfi1, int surfi2, 
                                       const EdgePointGeomInfo & ap1, 
                                       const EdgePointGeomInfo & ap2,
                                       Point<3> & newp, EdgePointGeomInfo & newgi) const
  {
    double s0, s1;

    newp = p1+secpoint*(p2-p1);
    if(ap1.edgenr > emap.Size() || ap1.edgenr == 0)
      return;
    gp_Pnt pnt(newp(0), newp(1), newp(2));
    GeomAPI_ProjectPointOnCurve proj(pnt, BRep_Tool::Curve(TopoDS::Edge(emap(ap1.edgenr)), s0, s1));
    pnt = proj.NearestPoint();
    newp = Point<3> (pnt.X(), pnt.Y(), pnt.Z());
    newgi = ap1;
  };


//    void OCCGeometry :: WriteOCC_STL(char * filename)
//    {
//       cout << "writing stl..."; cout.flush();
//       StlAPI_Writer writer;
//       writer.RelativeMode() = Standard_False;
// 
//       writer.SetDeflection(0.02);
//       writer.Write(shape,filename);
// 
//       cout << "done" << endl;
//    }


  void LoadOCCInto(OCCGeometry* occgeo, const char* filename)
  {
      static Timer timer_all("LoadOCC"); RegionTimer rtall(timer_all);
      static Timer timer_readfile("LoadOCC-ReadFile");
      static Timer timer_transfer("LoadOCC-Transfer");
      static Timer timer_getnames("LoadOCC-get names");

      // Initiate a dummy XCAF Application to handle the STEP XCAF Document
      static Handle(XCAFApp_Application) dummy_app = XCAFApp_Application::GetApplication();

      // Create an XCAF Document to contain the STEP file itself
      Handle(TDocStd_Document) step_doc;

      // Check if a STEP File is already open under this handle, if so, close it to prevent
      // Segmentation Faults when trying to create a new document
      if(dummy_app->NbDocuments() > 0)
      {
         dummy_app->GetDocument(1,step_doc);
         dummy_app->Close(step_doc);
      }
      dummy_app->NewDocument ("STEP-XCAF",step_doc);

      timer_readfile.Start();
      STEPCAFControl_Reader reader;

      // Enable transfer of colours
      reader.SetColorMode(Standard_True);
      reader.SetNameMode(Standard_True);
      Standard_Integer stat = reader.ReadFile((char*)filename);
      timer_readfile.Stop();

      timer_transfer.Start();
      if(stat != IFSelect_RetDone)
      {
        throw NgException("Couldn't load OCC geometry");
      }

      reader.Transfer(step_doc);
      timer_transfer.Stop();

      // Read in the shape(s) and the colours present in the STEP File
      auto step_shape_contents = XCAFDoc_DocumentTool::ShapeTool(step_doc->Main());

      TDF_LabelSequence step_shapes;
      step_shape_contents->GetShapes(step_shapes);

      // For the STEP File Reader in OCC, the 1st Shape contains the entire 
      // compound geometry as one shape
      auto main_shape = step_shape_contents->GetShape(step_shapes.Value(1)); 

      step_utils::LoadProperties(main_shape, reader, step_doc);

      occgeo->shape = main_shape;
      occgeo->changed = 1;
      occgeo->BuildFMap();
      occgeo->CalcBoundingBox();
      PrintContents (occgeo);
  }

   // Philippose - 23/02/2009
   /* Special IGES File load function including the ability
   to extract individual surface colours via the extended
   OpenCascade XDE and XCAF Feature set.
   */
   OCCGeometry *LoadOCC_IGES(const char *filename)
   {
      OCCGeometry *occgeo;
      occgeo = new OCCGeometry;
      // Initiate a dummy XCAF Application to handle the IGES XCAF Document
      static Handle(XCAFApp_Application) dummy_app = XCAFApp_Application::GetApplication();

      // Create an XCAF Document to contain the IGES file itself
      Handle(TDocStd_Document) iges_doc;

      // Check if a IGES File is already open under this handle, if so, close it to prevent
      // Segmentation Faults when trying to create a new document
      if(dummy_app->NbDocuments() > 0)
      {
         dummy_app->GetDocument(1,iges_doc);
         dummy_app->Close(iges_doc);
      }
      dummy_app->NewDocument ("IGES-XCAF",iges_doc);

      IGESCAFControl_Reader reader;

      Standard_Integer stat = reader.ReadFile((char*)filename);

      if(stat != IFSelect_RetDone)
      {
        throw NgException("Couldn't load occ");
      }

      // Enable transfer of colours
      reader.SetColorMode(Standard_True);

      reader.Transfer(iges_doc);

      // Read in the shape(s) and the colours present in the IGES File
      Handle(XCAFDoc_ShapeTool) iges_shape_contents = XCAFDoc_DocumentTool::ShapeTool(iges_doc->Main());
      Handle(XCAFDoc_ColorTool) iges_colour_contents = XCAFDoc_DocumentTool::ColorTool(iges_doc->Main());

      TDF_LabelSequence iges_shapes;
      iges_shape_contents->GetShapes(iges_shapes);

      // List out the available colours in the IGES File as Colour Names
      TDF_LabelSequence all_colours;
      iges_colour_contents->GetColors(all_colours);
      PrintMessage(1,"Number of colours in IGES File: ",all_colours.Length());
      for(int i = 1; i <= all_colours.Length(); i++)
      {
         Quantity_Color col;
         stringstream col_rgb;
         iges_colour_contents->GetColor(all_colours.Value(i),col);
         col_rgb << " : (" << col.Red() << "," << col.Green() << "," << col.Blue() << ")";
         PrintMessage(1, "Colour [", i, "] = ",col.StringName(col.Name()),col_rgb.str());
      }


      // For the IGES Reader, all the shapes can be exported as one compound shape
      // using the "OneShape" member
      occgeo->shape = reader.OneShape();
      occgeo->changed = 1;
      occgeo->BuildFMap();

      occgeo->CalcBoundingBox();
      PrintContents (occgeo);
      return occgeo;
   }





   // Philippose - 29/01/2009
   /* Special STEP File load function including the ability
   to extract individual surface colours via the extended
   OpenCascade XDE and XCAF Feature set.
   */
   OCCGeometry * LoadOCC_STEP (const char * filename)
   {
      OCCGeometry * occgeo;
      occgeo = new OCCGeometry;

      LoadOCCInto(occgeo, filename);
      return occgeo;
   }




   OCCGeometry *LoadOCC_BREP (const char *filename)
   {
      OCCGeometry * occgeo;
      occgeo = new OCCGeometry;

      BRep_Builder aBuilder;
      Standard_Boolean result = BRepTools::Read(occgeo->shape, const_cast<char*> (filename),aBuilder);

      if(!result)
      {
         delete occgeo;
         return NULL;
      }

      occgeo->changed = 1;
      occgeo->BuildFMap();

      occgeo->CalcBoundingBox();
      PrintContents (occgeo);

      return occgeo;
   }


  void OCCGeometry :: Save (string sfilename) const
  {
    const char * filename = sfilename.c_str();
    if (strlen(filename) < 4) 
      throw NgException ("illegal filename");
    
    if (strcmp (&filename[strlen(filename)-3], "igs") == 0)
      {
	IGESControl_Writer writer("millimeters", 1);
	writer.AddShape (shape);
	writer.Write (filename);
      }
    else if (strcmp (&filename[strlen(filename)-3], "stp") == 0)
      {
          step_utils::WriteSTEP(*this, filename);
      }
    else if (strcmp (&filename[strlen(filename)-3], "stl") == 0)
      {
	StlAPI_Writer writer;
	writer.ASCIIMode() = Standard_True;
	writer.Write (shape, filename);
      }
    else if (strcmp (&filename[strlen(filename)-4], "stlb") == 0)
      {
	StlAPI_Writer writer;
	writer.ASCIIMode() = Standard_False;
	writer.Write (shape, filename);
      }
  }

  void OCCGeometry :: DoArchive(Archive& ar)
  {
    if(ar.Output())
      {
        std::stringstream ss;
        BRepTools::Write(shape, ss);
        ar << ss.str();
      }
    else
      {
        std::string str;
        ar & str;
        stringstream ss(str);
        BRep_Builder builder;
        BRepTools::Read(shape, ss, builder);
      }

    ar & occdim;
    for (auto typ : { TopAbs_SOLID, TopAbs_FACE,  TopAbs_EDGE })
      for (TopExp_Explorer e(shape, typ); e.More(); e.Next())
        ar & global_shape_properties[e.Current().TShape()];

    if(ar.Input())
      {
        changed = 1;
        BuildFMap();
        CalcBoundingBox();
      }
  }
  
  const char * shapesname[] =
   {" ", "CompSolids", "Solids", "Shells",

   "Faces", "Wires", "Edges", "Vertices"};

  const char * shapename[] =
   {" ", "CompSolid", "Solid", "Shell",
   "Face", "Wire", "Edge", "Vertex"};

  const char * orientationstring[] =
     {"+", "-"};




   void OCCGeometry :: RecursiveTopologyTree (const TopoDS_Shape & sh,
      stringstream & str,
      TopAbs_ShapeEnum l,
      bool isfree,
      const char * lname)
   {
      if (l > TopAbs_VERTEX) return;

      TopExp_Explorer e;
      int count = 0;
      int count2 = 0;

      if (isfree)
         e.Init(sh, l, TopAbs_ShapeEnum(l-1));
      else
         e.Init(sh, l);

      for (; e.More(); e.Next())
      {
         count++;

         stringstream lname2;
         lname2 << lname << "/" << shapename[l] << count;
         str << lname2.str() << " ";

         switch (e.Current().ShapeType())
	   {
	   case TopAbs_SOLID:
	     count2 = somap.FindIndex(TopoDS::Solid(e.Current())); break;
	   case TopAbs_SHELL:
	     count2 = shmap.FindIndex(TopoDS::Shell(e.Current())); break;
	   case TopAbs_FACE:
	     count2 = fmap.FindIndex(TopoDS::Face(e.Current())); break;
	   case TopAbs_WIRE:
	     count2 = wmap.FindIndex(TopoDS::Wire(e.Current())); break;
	   case TopAbs_EDGE:
	     count2 = emap.FindIndex(TopoDS::Edge(e.Current())); break;
	   case TopAbs_VERTEX:
	     count2 = vmap.FindIndex(TopoDS::Vertex(e.Current())); break;
	   default:
	     cout << "RecursiveTopologyTree: Case " << e.Current().ShapeType() << " not handeled" << endl;
         }

         int nrsubshapes = 0;

         if (l <= TopAbs_WIRE)
         {
            TopExp_Explorer e2;
            for (e2.Init (e.Current(), TopAbs_ShapeEnum (l+1));
               e2.More(); e2.Next())
               nrsubshapes++;
         }

         str << "{" << shapename[l] << " " << count2;

         if (l <= TopAbs_EDGE)
         {
            str << " (" << orientationstring[e.Current().Orientation()];
            if (nrsubshapes != 0) str << ", " << nrsubshapes;
            str << ") } ";
         }
         else
            str << " } ";

         RecursiveTopologyTree (e.Current(), str, TopAbs_ShapeEnum (l+1),
            false, (char*)lname2.str().c_str());

      }
   }




   void OCCGeometry :: GetTopologyTree (stringstream & str)
   {
      cout << "Building topology tree ... " << flush;
      RecursiveTopologyTree (shape, str, TopAbs_COMPSOLID, false, "CompSolids");
      RecursiveTopologyTree (shape, str, TopAbs_SOLID, true, "FreeSolids");
      RecursiveTopologyTree (shape, str, TopAbs_SHELL, true, "FreeShells");
      RecursiveTopologyTree (shape, str, TopAbs_FACE, true, "FreeFaces");
      RecursiveTopologyTree (shape, str, TopAbs_WIRE, true, "FreeWires");
      RecursiveTopologyTree (shape, str, TopAbs_EDGE, true, "FreeEdges");
      RecursiveTopologyTree (shape, str, TopAbs_VERTEX, true, "FreeVertices");
      str << flush;
      //  cout << "done" << endl;
   }




   void OCCGeometry :: CheckIrregularEntities(stringstream & str)
   {
      ShapeAnalysis_CheckSmallFace csm;

      csm.SetTolerance (1e-6);

      TopTools_DataMapOfShapeListOfShape mapEdges;
      ShapeAnalysis_DataMapOfShapeListOfReal mapParam;
      TopoDS_Compound theAllVert;

      int spotfaces = 0;
      int stripsupportfaces = 0;
      int singlestripfaces = 0;
      int stripfaces = 0;
      int facessplitbyvertices = 0;
      int stretchedpinfaces = 0;
      int smoothpinfaces = 0;
      int twistedfaces = 0;
      // int edgessamebutnotidentified = 0;

      cout << "checking faces ... " << flush;

      int i;
      for (i = 1; i <= fmap.Extent(); i++)
      {
         TopoDS_Face face = TopoDS::Face (fmap(i));
         TopoDS_Edge e1, e2;

         if (csm.CheckSpotFace (face))
         {
            if (!spotfaces++)
               str << "SpotFace {Spot face} ";

            (*testout) << "Face " << i << " is a spot face" << endl;
            str << "SpotFace/Face" << i << " ";
            str << "{Face " << i << " } ";
         }

         if (csm.IsStripSupport (face))
         {
            if (!stripsupportfaces++)
               str << "StripSupportFace {Strip support face} ";

            (*testout) << "Face " << i << " has strip support" << endl;
            str << "StripSupportFace/Face" << i << " ";
            str << "{Face " << i << " } ";
         }

         if (csm.CheckSingleStrip(face, e1, e2))
         {
            if (!singlestripfaces++)
               str << "SingleStripFace {Single strip face} ";

            (*testout) << "Face " << i << " is a single strip (edge " << emap.FindIndex(e1)
               << " and edge " << emap.FindIndex(e2) << " are identical)" << endl;
            str << "SingleStripFace/Face" << i << " ";
            str << "{Face " << i << " (edge " << emap.FindIndex(e1)
               << " and edge " << emap.FindIndex(e2) << " are identical)} ";
         }

         if (csm.CheckStripFace(face, e1, e2))
         {
            if (!stripfaces++)
               str << "StripFace {Strip face} ";

            (*testout) << "Face " << i << " is a strip (edge " << emap.FindIndex(e1)
               << " and edge " << emap.FindIndex(e2)
               << " are identical)" << endl;
            str << "StripFace/Face" << i << " ";
            str << "{Face " << i << " (edge " << emap.FindIndex(e1)
               << " and edge " << emap.FindIndex(e2) << " are identical)} ";
         }

         if (int count = csm.CheckSplittingVertices(face, mapEdges, mapParam, theAllVert))
         {
            if (!facessplitbyvertices++)
               str << "FaceSplitByVertices {Face split by vertices} ";

            (*testout) << "Face " << i << " is split by " << count
               << " vertex/vertices " << endl;
            str << "FaceSplitByVertices/Face" << i << " ";
            str << "{Face " << i << " (split by " << count << "vertex/vertices)} ";
         }

         int whatrow, sens;
         if (int type = csm.CheckPin (face, whatrow, sens))
         {
            if (type == 1)
            {
               if (!smoothpinfaces++)
                  str << "SmoothPinFace {Smooth pin face} ";

               (*testout) << "Face " << i << " is a smooth pin" << endl;
               str << "SmoothPinFace/Face" << i << " ";
               str << "{Face " << i << " } ";
            }
            else
            {
               if (!stretchedpinfaces++)
                  str << "StretchedPinFace {Stretched pin face} ";

               (*testout) << "Face " << i << " is a stretched pin" << endl;
               str << "StretchedPinFace/Face" << i << " ";
               str << "{Face " << i << " } ";
            }
         }

         double paramu, paramv;
         if (csm.CheckTwisted (face, paramu, paramv))
         {
            if (!twistedfaces++)
               str << "TwistedFace {Twisted face} ";

            (*testout) << "Face " << i << " is twisted" << endl;
            str << "TwistedFace/Face" << i << " ";
            str << "{Face " << i << " } ";
         }
      }

      cout << "done" << endl;
      cout << "checking edges ... " << flush;

      // double dmax;
      // int cnt = 0;
      NgArray <double> edgeLengths;
      NgArray <int> order;
      edgeLengths.SetSize (emap.Extent());
      order.SetSize (emap.Extent());

      for (i = 1; i <= emap.Extent(); i++)
      {
         TopoDS_Edge edge1 = TopoDS::Edge (emap(i));
         GProp_GProps system;
         BRepGProp::LinearProperties(edge1, system);
         edgeLengths[i-1] = system.Mass();
      }

      Sort (edgeLengths, order);

      str << "ShortestEdges {Shortest edges} ";
      for (i = 1; i <= min(20, emap.Extent()); i++)
      {
         str << "ShortestEdges/Edge" << i;
         str << " {Edge " << order[i-1] << " (L=" << edgeLengths[order[i-1]-1] << ")} ";
      }

      str << flush;

      cout << "done" << endl;
   }




   void OCCGeometry :: GetUnmeshedFaceInfo (stringstream & str)
   {
      for (int i = 1; i <= fmap.Extent(); i++)
      {
         if (facemeshstatus[i-1] == -1)
            str << "Face" << i << " {Face " << i << " } ";
      }
      str << flush;
   }




   void OCCGeometry :: GetNotDrawableFaces (stringstream & str)
   {
      for (int i = 1; i <= fmap.Extent(); i++)
      {
         if (!fvispar[i-1].IsDrawable())
            str << "Face" << i << " {Face " << i << " } ";
      }
      str << flush;
   }




   bool OCCGeometry :: ErrorInSurfaceMeshing ()
   {
      for (int i = 1; i <= fmap.Extent(); i++)
         if (facemeshstatus[i-1] == -1)
            return true;

      return false;
   }

  void OCCParameters :: Print(ostream & ost) const
   {
      ost << "OCC Parameters:" << endl
		 << "minimum edge length: " << resthminedgelenenable
		 << ", min len = " << resthminedgelen << endl;
   }

  DLL_HEADER extern OCCParameters occparam;
  OCCParameters occparam;

  // int OCCGeometry :: GenerateMesh (shared_ptr<Mesh> & mesh, MeshingParameters & mparam)
  //  {
  //    return OCCGenerateMesh (*this, mesh, mparam, occparam);
  //  }
  static RegisterClassForArchive<OCCGeometry, NetgenGeometry> regnggeo;

  namespace step_utils
  {
      void LoadProperties(const TopoDS_Shape & shape,
                          const STEPCAFControl_Reader & reader,
                          const Handle(TDocStd_Document) step_doc)
      {
        static Timer t("step_utils::LoadProperties"); RegionTimer rt(t);

        auto workSession = reader.Reader().WS();
        auto model = workSession->Model();
        auto transferReader = workSession->TransferReader();
        auto transProc = transferReader->TransientProcess();
        auto shapeTool = XCAFDoc_DocumentTool::ShapeTool(step_doc->Main());

        // load colors
        for (auto typ : { TopAbs_SOLID, TopAbs_FACE,  TopAbs_EDGE })
          for (TopExp_Explorer e(shape, typ); e.More(); e.Next())
          {
            TDF_Label label;
            shapeTool->Search(e.Current(), label);

            if(label.IsNull())
                continue;

            XCAFPrs_IndexedDataMapOfShapeStyle set;
            TopLoc_Location loc;
            XCAFPrs::CollectStyleSettings(label, loc, set);
            XCAFPrs_Style aStyle;
            set.FindFromKey(e.Current(), aStyle);

            auto & prop = OCCGeometry::global_shape_properties[e.Current().TShape()];
            if(aStyle.IsSetColorSurf())
                prop.col = step_utils::ReadColor(aStyle.GetColorSurfRGBA());
          }

        // load names
        Standard_Integer nb = model->NbEntities();
        for (Standard_Integer i = 1; i <= nb; i++)
          {
            Handle(Standard_Transient) entity = model->Value(i);
            auto item = Handle(StepRepr_RepresentationItem)::DownCast(entity);

            if(item.IsNull())
                continue;

            TopoDS_Shape shape = TransferBRep::ShapeResult(transProc->Find(item));
            string name = item->Name()->ToCString();
            if (!transProc->IsBound(item))
              continue;

            OCCGeometry::global_shape_properties[shape.TShape()].name = name;
          }


        // load custom data (maxh etc.)
        for (Standard_Integer i = 1; i <= nb; i++)
          {
            Handle(Standard_Transient) entity = model->Value(i);

            auto item = Handle(StepRepr_CompoundRepresentationItem)::DownCast(entity);

            if(item.IsNull())
                continue;

            auto shape_item = item->ItemElementValue(1);
            TopoDS_Shape shape = TransferBRep::ShapeResult(transProc->Find(shape_item));
            string name = item->Name()->ToCString();

            if(name == "netgen_geometry_identification")
                ReadIdentifications(item, transProc);

            if(name != "netgen_geometry_properties")
                continue;

            auto & prop = OCCGeometry::global_shape_properties[shape.TShape()];

            auto nprops = item->NbItemElement();

            for(auto i : Range(2, nprops+1))
            {
                auto prop_item = item->ItemElementValue(i);
                string prop_name = prop_item->Name()->ToCString();

                if(prop_name=="maxh")
                    prop.maxh = Handle(StepRepr_ValueRepresentationItem)::DownCast(prop_item)
                        ->ValueComponentMember()->Real();

                if(prop_name=="hpref")
                    prop.hpref = Handle(StepRepr_ValueRepresentationItem)::DownCast(prop_item)
                        ->ValueComponentMember()->Real();
            }
          }
      }

      void WriteProperties(const Handle(Interface_InterfaceModel) model, const Handle(Transfer_FinderProcess) finder, const TopoDS_Shape & shape)
      {
          static const ShapeProperties default_props;
          Handle(StepRepr_RepresentationItem) item = STEPConstruct::FindEntity(finder, shape);
          if(!item)
              return;
          auto prop = OCCGeometry::global_shape_properties[shape.TShape()];

          if(auto n = prop.name)
              item->SetName(MakeName(*n));

          Array<Handle(StepRepr_RepresentationItem)> props;
          props.Append(item);

          if(auto maxh = prop.maxh; maxh != default_props.maxh)
              props.Append( MakeReal(maxh, "maxh") );

          if(auto hpref = prop.hpref; hpref != default_props.hpref)
              props.Append( MakeReal(hpref, "hpref") );

          if(props.Size()>1)
          {
              for(auto & item : props.Range(1, props.Size()))
                  model->AddEntity(item);

              auto compound = MakeCompound(props, "netgen_geometry_properties");
              model->AddEntity(compound);
          }

          WriteIdentifications(model, shape, finder);
      }

      void WriteIdentifications(const Handle(Interface_InterfaceModel) model, const TopoDS_Shape & shape, const Handle(Transfer_FinderProcess) finder)
      {
          Handle(StepRepr_RepresentationItem) item = STEPConstruct::FindEntity(finder, shape);
          auto & identifications = OCCGeometry::identifications[shape.TShape()];
          if(identifications.size()==0)
              return;
          auto n = identifications.size();
          Array<Handle(StepRepr_RepresentationItem)> ident_items;
          ident_items.Append(item);

          for(auto & ident : identifications)
          {
              Array<Handle(StepRepr_RepresentationItem)> items;
              items.Append(STEPConstruct::FindEntity(finder, ident.other));
              items.Append(MakeInt(static_cast<int>(ident.inverse)));
              auto & m = ident.trafo.GetMatrix();
              for(auto i : Range(9))
                  items.Append(MakeReal(m(i)));
              auto & v = ident.trafo.GetVector();
              for(auto i : Range(3))
                  items.Append(MakeReal(v(i)));
              for(auto & item : items.Range(1,items.Size()))
                  model->AddEntity(item);
              ident_items.Append(MakeCompound(items, ident.name));
          }

          for(auto & item : ident_items.Range(1,ident_items.Size()))
              model->AddEntity(item);
          auto comp = MakeCompound(ident_items, "netgen_geometry_identification");
          model->AddEntity(comp);
      }

      void ReadIdentifications(Handle(StepRepr_RepresentationItem) item, Handle(Transfer_TransientProcess) transProc)
      {
          auto idents = Handle(StepRepr_CompoundRepresentationItem)::DownCast(item);
          auto n = idents->NbItemElement();
          std::vector<OCCIdentification> result;
          auto shape_origin = TransferBRep::ShapeResult(transProc->Find(idents->ItemElementValue(1)));

          for(auto i : Range(2,n+1))
          {
              auto id_item = Handle(StepRepr_CompoundRepresentationItem)::DownCast(idents->ItemElementValue(i));
              OCCIdentification ident;
              ident.name = id_item->Name()->ToCString();
              ident.other = TransferBRep::ShapeResult(transProc->Find(id_item->ItemElementValue(1)));
              ident.inverse = static_cast<bool>(ReadInt(id_item->ItemElementValue(2)));

              auto & m = ident.trafo.GetMatrix();
              for(auto i : Range(9))
                  m(i) = ReadReal(id_item->ItemElementValue(3+i));
              auto & v = ident.trafo.GetVector();
              for(auto i : Range(3))
                  v(i) = ReadReal(id_item->ItemElementValue(12+i));

              result.push_back(ident);
          }
          OCCGeometry::identifications[shape_origin.TShape()] = result;
      }

      void WriteSTEP(const TopoDS_Shape & shape, string filename)
      {
          Interface_Static::SetCVal("write.step.schema", "AP242IS");
          Interface_Static::SetIVal("write.step.assembly",1);
          Handle(XCAFApp_Application) app = XCAFApp_Application::GetApplication();
          Handle(TDocStd_Document) doc;

          app->NewDocument("STEP-XCAF", doc);
          Handle(XCAFDoc_ShapeTool) shapetool = XCAFDoc_DocumentTool::ShapeTool(doc->Main());
          Handle(XCAFDoc_ColorTool) colortool = XCAFDoc_DocumentTool::ColorTool(doc->Main());
          TDF_Label label = shapetool->NewShape();
          shapetool->SetShape(label, shape);

          Handle(XSControl_WorkSession) session = new XSControl_WorkSession;
          STEPCAFControl_Writer writer(session);
          const Handle(Interface_InterfaceModel) model = session->Model();

          // Set colors (BEFORE transferring shape into step data structures)
          for (auto typ : { TopAbs_SOLID, TopAbs_FACE,  TopAbs_EDGE })
            for (TopExp_Explorer e(shape, typ); e.More(); e.Next())
              {
                auto prop = OCCGeometry::global_shape_properties[e.Current().TShape()];
                if(auto col = prop.col)
                    colortool->SetColor(e.Current(), step_utils::MakeColor(*col), XCAFDoc_ColorGen);
              }

          // Transfer shape into step data structures -> now we can manipulate/add step representation items
          writer.Transfer(doc, STEPControl_AsIs);

          // Write all other properties
          auto finder = session->TransferWriter()->FinderProcess();

          for (auto typ : { TopAbs_SOLID, TopAbs_FACE,  TopAbs_EDGE })
            for (TopExp_Explorer e(shape, typ); e.More(); e.Next())
                WriteProperties(model, finder, e.Current());

          writer.Write(filename.c_str());
      }

  } // namespace step_utils
}


#endif
