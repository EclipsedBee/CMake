/*=========================================================================

  Program:   CMake - Cross-Platform Makefile Generator
  Module:    $RCSfile$
  Language:  C++
  Date:      $Date$
  Version:   $Revision$

  Copyright (c) 2002 Kitware, Inc., Insight Consortium.  All rights reserved.
  See Copyright.txt or http://www.cmake.org/HTML/Copyright.html for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notices for more information.

=========================================================================*/
#include "cmInstallTargetGenerator.h"

#include "cmGlobalGenerator.h"
#include "cmLocalGenerator.h"
#include "cmMakefile.h"
#include "cmake.h"

//----------------------------------------------------------------------------
cmInstallTargetGenerator
::cmInstallTargetGenerator(cmTarget& t, const char* dest, bool implib,
                           const char* file_permissions,
                           std::vector<std::string> const& configurations,
                           const char* component, bool optional):
  cmInstallGenerator(dest), Target(&t), ImportLibrary(implib),
  FilePermissions(file_permissions), Configurations(configurations),
  Component(component), Optional(optional)
{
  this->Target->SetHaveInstallRule(true);
}

//----------------------------------------------------------------------------
cmInstallTargetGenerator
::~cmInstallTargetGenerator()
{
}

//----------------------------------------------------------------------------
void cmInstallTargetGenerator::GenerateScript(std::ostream& os)
{
  // Compute the build tree directory from which to copy the target.
  std::string fromDir;
  if(this->Target->NeedRelinkBeforeInstall())
    {
    fromDir = this->Target->GetMakefile()->GetStartOutputDirectory();
    fromDir += cmake::GetCMakeFilesDirectory();
    fromDir += "/CMakeRelink.dir/";
    }
  else
    {
    fromDir = this->Target->GetDirectory(0, this->ImportLibrary);
    fromDir += "/";
    }

  // Write variable settings to do per-configuration references.
  this->PrepareScriptReference(os, this->Target, "BUILD", true, this->ImportLibrary, false);

  // Create the per-configuration reference.
  std::string fromName = this->GetScriptReference(this->Target, "BUILD",
                                                  this->ImportLibrary, false);
  std::string fromFile = fromDir;
  fromFile += fromName;

  // Choose the final destination.  This may be modified for certain
  // target types.
  std::string destination = this->Destination;

  // Setup special properties for some target types.
  std::string literal_args;
  std::string props;
  const char* properties = 0;
  cmTarget::TargetType type = this->Target->GetType();
  switch(type)
    {
    case cmTarget::SHARED_LIBRARY:
      {
      // Add shared library installation properties if this platform
      // supports them.
      const char* lib_version = 0;
      const char* lib_soversion = 0;

      // Versioning is supported only for shared libraries and modules,
      // and then only when the platform supports an soname flag.
      cmGlobalGenerator* gg =
        (this->Target->GetMakefile()
         ->GetLocalGenerator()->GetGlobalGenerator());
      if(const char* linkLanguage = this->Target->GetLinkerLanguage(gg))
        {
        std::string sonameFlagVar = "CMAKE_SHARED_LIBRARY_SONAME_";
        sonameFlagVar += linkLanguage;
        sonameFlagVar += "_FLAG";
        if(this->Target->GetMakefile()->GetDefinition(sonameFlagVar.c_str()))
          {
          lib_version = this->Target->GetProperty("VERSION");
          lib_soversion = this->Target->GetProperty("SOVERSION");
          }
        }

      if(lib_version)
        {
        props += " VERSION ";
        props += lib_version;
        }
      if(lib_soversion)
        {
        props += " SOVERSION ";
        props += lib_soversion;
        }
      properties = props.c_str();
      }
      break;
    case cmTarget::EXECUTABLE:
      {
      // Add executable installation properties if this platform
      // supports them.
#if defined(_WIN32) && !defined(__CYGWIN__)
      const char* exe_version = 0;
#else
      const char* exe_version = this->Target->GetProperty("VERSION");
#endif
      if(exe_version)
        {
        props += " VERSION ";
        props += exe_version;
        properties = props.c_str();
        }

      // Handle OSX Bundles.
      if(this->Target->GetMakefile()->IsOn("APPLE") &&
         this->Target->GetPropertyAsBool("MACOSX_BUNDLE"))
        {
        // Compute the source locations of the bundle executable and
        // Info.plist file.
        this->PrepareScriptReference(os, this->Target, "INSTALL",
                                     false, this->ImportLibrary, false);
        fromFile += ".app";
        type = cmTarget::INSTALL_DIRECTORY;
        literal_args += " USE_SOURCE_PERMISSIONS";
        }
      }
      break;
    case cmTarget::STATIC_LIBRARY:
    case cmTarget::MODULE_LIBRARY:
      // Nothing special for modules or static libraries.
      break;
    default:
      break;
    }

  // An import library looks like a static library.
  if(this->ImportLibrary)
    {
    type = cmTarget::STATIC_LIBRARY;
    }

  // Write code to install the target file.
  const char* no_dir_permissions = 0;
  const char* no_rename = 0;
  bool optional = this->Optional | this->ImportLibrary;
  this->AddInstallRule(os, destination.c_str(), type, fromFile.c_str(),
                       optional, properties,
                       this->FilePermissions.c_str(), no_dir_permissions,
                       this->Configurations,
                       this->Component.c_str(),
                       no_rename, literal_args.c_str());

  // Fix the install_name settings in installed binaries.
  if((type == cmTarget::SHARED_LIBRARY ||
     type == cmTarget::MODULE_LIBRARY ||
     type == cmTarget::EXECUTABLE))
    {
    this->AddInstallNamePatchRule(os, destination.c_str());
    }

  std::string quotedFullDestinationFilename = "\"$ENV{DESTDIR}";
  quotedFullDestinationFilename += destination;
  quotedFullDestinationFilename += "/";
  quotedFullDestinationFilename += cmSystemTools::GetFilenameName(fromFile);
  quotedFullDestinationFilename += "\"";

  this->AddRanlibRule(os, type, quotedFullDestinationFilename);

  this->AddStripRule(os, type, quotedFullDestinationFilename, optional);
}


std::string cmInstallTargetGenerator::GetInstallFilename(const char* config) const
{
  return cmInstallTargetGenerator::GetInstallFilename(this->Target, config, this->ImportLibrary, false);
}

//----------------------------------------------------------------------------
std::string cmInstallTargetGenerator::GetInstallFilename(cmTarget* target,
                                                         const char* config,
                                                         bool implib,
                                                         bool useSOName)
{
  std::string fname;
  // Compute the name of the library.
  if(target->GetType() == cmTarget::EXECUTABLE)
    {
    std::string targetName;
    std::string targetNameReal;
    std::string targetNameImport;
    std::string targetNamePDB;
    target->GetExecutableNames(targetName, targetNameReal,
                               targetNameImport, targetNamePDB,
                               config);
    if(implib)
      {
      // Use the import library name.
      fname = targetNameImport;
      }
    else
      {
      // Use the canonical name.
      fname = targetName;
      }
    }
  else
    {
    std::string targetName;
    std::string targetNameSO;
    std::string targetNameReal;
    std::string targetNameImport;
    std::string targetNamePDB;
    target->GetLibraryNames(targetName, targetNameSO, targetNameReal,
                            targetNameImport, targetNamePDB, config);
    if(implib)
      {
      // Use the import library name.
      fname = targetNameImport;
      }
    else if(useSOName)
      {
      // Use the soname.
      fname = targetNameSO;
      }
    else
      {
      // Use the canonical name.
      fname = targetName;
      }
    }

  return fname;
}

//----------------------------------------------------------------------------
void
cmInstallTargetGenerator
::PrepareScriptReference(std::ostream& os, cmTarget* target,
                         const char* place, bool useConfigDir,
                         bool implib, bool useSOName)
{
  // If the target name may vary with the configuration type then
  // store all possible names ahead of time in variables.
  std::string fname;
  for(std::vector<std::string>::const_iterator i =
        this->ConfigurationTypes->begin();
      i != this->ConfigurationTypes->end(); ++i)
    {
    // Initialize the name.
    fname = "";

    if(useConfigDir)
      {
      // Start with the configuration's subdirectory.
      target->GetMakefile()->GetLocalGenerator()->GetGlobalGenerator()->
        AppendDirectoryForConfig("", i->c_str(), "/", fname);
      }

    fname += this->GetInstallFilename(target, i->c_str(), 
                                      implib, useSOName);

    // Set a variable with the target name for this configuration.
    os << "SET(" << target->GetName() << "_" << place
       << (implib? "_IMPNAME_" : "_NAME_") << *i
       << " \"" << fname << "\")\n";
    }
}

//----------------------------------------------------------------------------
std::string cmInstallTargetGenerator::GetScriptReference(cmTarget* target,
                                                         const char* place,
                                                         bool implib, bool useSOName)
{
  if(this->ConfigurationTypes->empty())
    {
    // Reference the target by its one configuration name.
    return this->GetInstallFilename(target, this->ConfigurationName, 
                                    implib, useSOName);
    }
  else
    {
    // Reference the target using the per-configuration variable.
    std::string ref = "${";
    ref += target->GetName();
    if(implib)
      {
      ref += "_";
      ref += place;
      ref += "_IMPNAME_";
      }
    else
      {
      ref += "_";
      ref += place;
      ref += "_NAME_";
      }
    ref += "${CMAKE_INSTALL_CONFIG_NAME}}";
    return ref;
    }
}

//----------------------------------------------------------------------------
void cmInstallTargetGenerator
::AddInstallNamePatchRule(std::ostream& os,
                          const char* destination)
{
  std::string installNameTool =
    this->Target->GetMakefile()->GetSafeDefinition("CMAKE_INSTALL_NAME_TOOL");
  
  if(!installNameTool.size())
    {
    return;
    }

  // Build a map of build-tree install_name to install-tree install_name for
  // shared libraries linked to this target.
  std::map<cmStdString, cmStdString> install_name_remap;
  cmTarget::LinkLibraryType linkType = cmTarget::OPTIMIZED;
  const char* config = this->ConfigurationName;
  if(config && cmSystemTools::UpperCase(config) == "DEBUG")
    {
    linkType = cmTarget::DEBUG;
    }
  // TODO: Merge with ComputeLinkInformation.
  const cmTarget::LinkLibraryVectorType& inLibs = 
    this->Target->GetLinkLibraries();
  for(cmTarget::LinkLibraryVectorType::const_iterator j = inLibs.begin();
      j != inLibs.end(); ++j)
    {
    std::string lib = j->first;
    if((this->Target->GetType() == cmTarget::EXECUTABLE ||
        lib != this->Target->GetName()) &&
       (j->second == cmTarget::GENERAL || j->second == linkType))
      {
      if(cmTarget* tgt = this->Target->GetMakefile()->
         GetLocalGenerator()->GetGlobalGenerator()->
         FindTarget(0, lib.c_str(), false))
        {
        if(tgt->GetType() == cmTarget::SHARED_LIBRARY)
          {
          // If the build tree and install tree use different path
          // components of the install_name field then we need to create a
          // mapping to be applied after installation.
          std::string for_build = tgt->GetInstallNameDirForBuildTree(config);
          std::string for_install = 
            tgt->GetInstallNameDirForInstallTree(config);
          if(for_build != for_install)
            {
            // Map from the build-tree install_name.
            this->PrepareScriptReference(os, tgt, "REMAP_FROM",
                                         !for_build.empty(), false, true);
            for_build += this->GetScriptReference(tgt, "REMAP_FROM", false, true);

            // Map to the install-tree install_name.
            this->PrepareScriptReference(os, tgt, "REMAP_TO",
                                         false, false, true);
            for_install += this->GetScriptReference(tgt, "REMAP_TO", false, true);

            // Store the mapping entry.
            install_name_remap[for_build] = for_install;
            }
          }
        }
      }
    }

  // Edit the install_name of the target itself if necessary.
  this->PrepareScriptReference(os, this->Target, "REMAPPED", false, this->ImportLibrary, true);
  std::string new_id;
  if(this->Target->GetType() == cmTarget::SHARED_LIBRARY)
    {
    std::string for_build = 
      this->Target->GetInstallNameDirForBuildTree(config);
    std::string for_install = 
      this->Target->GetInstallNameDirForInstallTree(config);
    if(for_build != for_install)
      {
      // Prepare to refer to the install-tree install_name.
      new_id = for_install;
      new_id += this->GetScriptReference(this->Target, "REMAPPED", this->ImportLibrary, true);
      }
    }

  // Write a rule to run install_name_tool to set the install-tree
  // install_name value and references.
  if(!new_id.empty() || !install_name_remap.empty())
    {
    std::string component_test = "NOT CMAKE_INSTALL_COMPONENT OR "
      "\"${CMAKE_INSTALL_COMPONENT}\" MATCHES \"^(";
    component_test += this->Component;
    component_test += ")$\"";
    os << "IF(" << component_test << ")\n";
    os << "  EXECUTE_PROCESS(COMMAND \"" << installNameTool;
    os << "\"";
    if(!new_id.empty())
      {
      os << "\n    -id \"" << new_id << "\"";
      }
    for(std::map<cmStdString, cmStdString>::const_iterator
          i = install_name_remap.begin();
        i != install_name_remap.end(); ++i)
      {
      os << "\n    -change \"" << i->first << "\" \"" << i->second << "\"";
      }
    os << "\n    \"$ENV{DESTDIR}" << destination << "/"
       << this->GetScriptReference(this->Target, "REMAPPED", this->ImportLibrary, true) << "\")\n";
    os << "ENDIF(" << component_test << ")\n";
    }
}

void cmInstallTargetGenerator::AddStripRule(std::ostream& os, 
                              cmTarget::TargetType type,
                              const std::string& quotedFullDestinationFilename,
                              bool optional)
{

  // don't strip static libraries, because it removes the only symbol table
  // they have so you can't link to them anymore
  if(type == cmTarget::STATIC_LIBRARY)
    {
    return;
    }

  // Don't handle OSX Bundles.
  if(this->Target->GetMakefile()->IsOn("APPLE") &&
     this->Target->GetPropertyAsBool("MACOSX_BUNDLE"))
    {
    return;
    }

  if(! this->Target->GetMakefile()->IsSet("CMAKE_STRIP"))
    {
    return;
    }

  std::string optionalString;
  if (optional)
    {
    optionalString = " AND EXISTS ";
    optionalString += quotedFullDestinationFilename;
    }

  os << "IF(CMAKE_INSTALL_DO_STRIP" << optionalString << ")\n";
  os << "  EXECUTE_PROCESS(COMMAND \""
     << this->Target->GetMakefile()->GetDefinition("CMAKE_STRIP")
     << "\" " << quotedFullDestinationFilename << " )\n";
  os << "ENDIF(CMAKE_INSTALL_DO_STRIP" << optionalString << ")\n";
}

void cmInstallTargetGenerator::AddRanlibRule(std::ostream& os, 
                              cmTarget::TargetType type,
                              const std::string& quotedFullDestinationFilename)
{
  // Static libraries need ranlib on this platform.
  if(type != cmTarget::STATIC_LIBRARY)
    {
    return;
    }

  // Perform post-installation processing on the file depending
  // on its type.
  if(!this->Target->GetMakefile()->IsOn("APPLE"))
    {
    return;
    }

  std::string ranlib = this->Target->GetMakefile()->GetRequiredDefinition(
                                                    "CMAKE_RANLIB");
  if (!ranlib.size())
    {
    return;
    }

  os << "EXECUTE_PROCESS(COMMAND \"";
  os << ranlib;
  os << "\" " << quotedFullDestinationFilename << " )\n";
}
