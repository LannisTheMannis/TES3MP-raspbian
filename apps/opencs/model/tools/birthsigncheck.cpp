#include "birthsigncheck.hpp"

#include <components/esm/loadbsgn.hpp>
#include <components/misc/resourcehelpers.hpp>

#include "../prefs/state.hpp"

#include "../world/universalid.hpp"


std::string CSMTools::BirthsignCheckStage::checkTexture(const std::string &texture) const
{
    if (mTextures.searchId(texture) != -1) return std::string();

    std::string ddsTexture = texture;
    if (Misc::ResourceHelpers::changeExtensionToDds(ddsTexture) && mTextures.searchId(ddsTexture) != -1) return std::string();

    return "Image '" + texture + "' does not exist";
}

CSMTools::BirthsignCheckStage::BirthsignCheckStage (const CSMWorld::IdCollection<ESM::BirthSign>& birthsigns,
                                                    const CSMWorld::Resources &textures)
: mBirthsigns(birthsigns),
  mTextures(textures)
{
    mIgnoreBaseRecords = false;
}

int CSMTools::BirthsignCheckStage::setup()
{
    mIgnoreBaseRecords = CSMPrefs::get()["Reports"]["ignore-base-records"].isTrue();

    return mBirthsigns.getSize();
}

void CSMTools::BirthsignCheckStage::perform (int stage, CSMDoc::Messages& messages)
{
    const CSMWorld::Record<ESM::BirthSign>& record = mBirthsigns.getRecord (stage);

    // Skip "Base" records (setting!) and "Deleted" records
    if ((mIgnoreBaseRecords && record.mState == CSMWorld::RecordBase::State_BaseOnly) || record.isDeleted())
        return;

    const ESM::BirthSign& birthsign = record.get();

    CSMWorld::UniversalId id (CSMWorld::UniversalId::Type_Birthsign, birthsign.mId);

    if (birthsign.mName.empty())
    {
        messages.add(id, "Name is missing", "", CSMDoc::Message::Severity_Error);
    }

    if (birthsign.mDescription.empty())
    {
        messages.add(id, "Description is missing", "", CSMDoc::Message::Severity_Warning);
    }

    if (birthsign.mTexture.empty())
    {
        messages.add(id, "Image is missing", "", CSMDoc::Message::Severity_Error);
    }
    else
    {
        const std::string error = checkTexture(birthsign.mTexture);
        if (!error.empty())
            messages.add(id, error, "", CSMDoc::Message::Severity_Error);
    }

    /// \todo check data members that can't be edited in the table view
}
