#include "global.h"
#include <cerrno>
#include <cstring>
#include "NotesWriterSSC.h"
#include "BackgroundUtil.h"
#include "Foreach.h"
#include "GameManager.h"
#include "LocalizedString.h"
#include "NoteTypes.h"
#include "Profile.h"
#include "ProfileManager.h"
#include "RageFile.h"
#include "RageFileManager.h"
#include "RageLog.h"
#include "RageUtil.h"
#include "Song.h"
#include "Steps.h"

/**
 * @brief Turn a vector of lines into a single line joined by newline characters.
 * @param lines the list of lines to join.
 * @return the joined lines. */
static RString JoinLineList( vector<RString> &lines )
{
	for( unsigned i = 0; i < lines.size(); ++i )
		TrimRight( lines[i] );

	// Skip leading blanks.
	unsigned j = 0;
	while( j < lines.size() && lines.size() == 0 )
		++j;

	return join( "\r\n", lines.begin()+j, lines.end() );
}


// A utility class to write timing tags more easily!
struct TimingTagWriter {

	vector<RString> *m_pvsLines;
	RString m_sNext;

	TimingTagWriter( vector<RString> *pvsLines ): m_pvsLines (pvsLines) { }

	void Write( const int row, const char *value )
	{
		m_pvsLines->push_back( m_sNext + ssprintf( "%.3f=%s", NoteRowToBeat(row), value ) );
		m_sNext = ",";
	}

	void Write( const int row, const float value )        { Write( row, ssprintf( "%.3f",  value ) ); }
	void Write( const int row, const int value )          { Write( row, ssprintf( "%d",    value ) ); }
	void Write( const int row, const int a, const int b ) { Write( row, ssprintf( "%d=%d", a, b ) );  }
	void Write( const int row, const float a, const float b ) { Write( row, ssprintf( "%.3f=%.3f", a, b) ); }
	void Write( const int row, const float a, const float b, const unsigned short c )
		{ Write( row, ssprintf( "%.3f=%.3f=%hd", a, b, c) ); }

	void Init( const RString sTag ) { m_sNext = "#" + sTag + ":"; }
	void Finish( ) { m_pvsLines->push_back( ( m_sNext != "," ? m_sNext : "" ) + ";" ); }

};

static void GetTimingTags( vector<RString> &lines, TimingData timing, bool bIsSong = false )
{
	TimingTagWriter w ( &lines );
	
	timing.TidyUpData();
	unsigned i = 0;

	w.Init( "BPMS" );
	vector<TimingSegment *> &bpms = timing.allTimingSegments[SEGMENT_BPM];
	for (; i < bpms.size(); i++)
	{
		BPMSegment *bs = static_cast<BPMSegment *>(bpms[i]);
		w.Write( bs->GetRow(), bs->GetBPM() );
	}
	w.Finish();
	
	w.Init( "STOPS" );
	vector<TimingSegment *> &stops = timing.allTimingSegments[SEGMENT_STOP];
	for (i = 0; i < stops.size(); i++)
	{
		StopSegment *ss = static_cast<StopSegment *>(stops[i]);
		w.Write( ss->GetRow(), ss->GetPause() );
	}
	w.Finish();
	
	w.Init( "DELAYS" );
	vector<TimingSegment *> &delays = timing.allTimingSegments[SEGMENT_DELAY];
	for (i = 0; i < delays.size(); i++)
	{
		DelaySegment *ss = static_cast<DelaySegment *>(delays[i]);
		w.Write( ss->GetRow(), ss->GetPause() );
	}
	w.Finish();
	
	w.Init( "WARPS" );
	vector<TimingSegment *> &warps = timing.allTimingSegments[SEGMENT_WARP];
	for (i = 0; i < warps.size(); i++)
	{
		WarpSegment *ws = static_cast<WarpSegment *>(warps[i]);
		w.Write( ws->GetRow(), ws->GetLength() );
	}
	w.Finish();
	
	vector<TimingSegment *> &tSigs = timing.allTimingSegments[SEGMENT_TIME_SIG];
	ASSERT( !tSigs.empty() );
	w.Init( "TIMESIGNATURES" );
	for (i = 0; i < tSigs.size(); i++)
	{
		TimeSignatureSegment *ts = static_cast<TimeSignatureSegment *>(tSigs[i]);
		w.Write( ts->GetRow(), ts->GetNum(), ts->GetDen() );
	}
	w.Finish();

	vector<TimingSegment *> &ticks = timing.allTimingSegments[SEGMENT_TICKCOUNT];
	ASSERT( !ticks.empty() );
	w.Init( "TICKCOUNTS" );
	for (i = 0; i < ticks.size(); i++)
	{
		TickcountSegment *ts = static_cast<TickcountSegment *>(ticks[i]);
		w.Write( ts->GetRow(), ts->GetTicks() );
	}
	w.Finish();
	
	vector<TimingSegment *> &combos = timing.allTimingSegments[SEGMENT_COMBO];
	ASSERT( !combos.empty() );
	w.Init( "COMBOS" );
	for (i = 0; i < combos.size(); i++)
	{
		ComboSegment *cs = static_cast<ComboSegment *>(combos[i]);
		if (cs->GetCombo() == cs->GetMissCombo())
			w.Write( cs->GetRow(), cs->GetCombo() );
		else
			w.Write( cs->GetRow(), cs->GetCombo(), cs->GetMissCombo() );
	}
	w.Finish();
	
	// Song Timing should only have the initial value.
	vector<TimingSegment *> &speeds = timing.allTimingSegments[SEGMENT_SPEED];
	w.Init( "SPEEDS" );
	for (i = 0; i < speeds.size(); i++)
	{
		SpeedSegment *ss = static_cast<SpeedSegment *>(speeds[i]);
		w.Write( ss->GetRow(), ss->GetRatio(), ss->GetLength(), ss->GetUnit() );
	}
	w.Finish();
	
	w.Init( "SCROLLS" );
	vector<TimingSegment *> &scrolls = timing.allTimingSegments[SEGMENT_SCROLL];
	for (i = 0; i < scrolls.size(); i++)
	{
		ScrollSegment *ss = static_cast<ScrollSegment *>(scrolls[i]);
		w.Write( ss->GetRow(), ss->GetRatio() );
	}
	w.Finish();
	
	if( !bIsSong )
	{	
		vector<TimingSegment *> &fakes = timing.allTimingSegments[SEGMENT_FAKE];
		w.Init( "FAKES" );
		for (i = 0; i < fakes.size(); i++)
		{
			FakeSegment *fs = static_cast<FakeSegment *>(fakes[i]);
			w.Write( fs->GetRow(), fs->GetLength() );
		}
		w.Finish();
	}
	
	w.Init( "LABELS" );
	vector<TimingSegment *> &labels = timing.allTimingSegments[SEGMENT_LABEL];
	for (i = 0; i < labels.size(); i++)
	{
		LabelSegment *ls = static_cast<LabelSegment *>(labels[i]);
		w.Write( ls->GetRow(), ls->GetLabel().c_str() );
	}
	w.Finish();
}

static void WriteTimingTags( RageFile &f, const TimingData &timing, bool bIsSong = false )
{
	f.PutLine(ssprintf("#BPMS:%s;",
			   join(",\r\n", timing.ToVectorString(SEGMENT_BPM)).c_str()));
	f.PutLine(ssprintf("#STOPS:%s;",
			   join(",\r\n", timing.ToVectorString(SEGMENT_STOP)).c_str()));
	f.PutLine(ssprintf("#DELAYS:%s;",
			   join(",\r\n", timing.ToVectorString(SEGMENT_DELAY)).c_str()));
	f.PutLine(ssprintf("#WARPS:%s;",
			   join(",\r\n", timing.ToVectorString(SEGMENT_WARP)).c_str()));
	f.PutLine(ssprintf("#TIMESIGNATURES:%s;",
			   join(",\r\n", timing.ToVectorString(SEGMENT_TIME_SIG)).c_str()));
	f.PutLine(ssprintf("#TICKCOUNTS:%s;",
			   join(",\r\n", timing.ToVectorString(SEGMENT_TICKCOUNT)).c_str()));
	f.PutLine(ssprintf("#COMBOS:%s;",
			   join(",\r\n", timing.ToVectorString(SEGMENT_COMBO)).c_str()));
	f.PutLine(ssprintf("#SPEEDS:%s;",
			   join(",\r\n", timing.ToVectorString(SEGMENT_SPEED)).c_str()));
	f.PutLine(ssprintf("#SCROLLS:%s;",
			   join(",\r\n", timing.ToVectorString(SEGMENT_SCROLL)).c_str()));
	if (!bIsSong)
		f.PutLine(ssprintf("#FAKES:%s;",
				   join(",\r\n", timing.ToVectorString(SEGMENT_FAKE)).c_str()));
	f.PutLine(ssprintf("#LABELS:%s;",
			   join(",\r\n", timing.ToVectorString(SEGMENT_LABEL)).c_str()));

}

/**
 * @brief Write out the common tags for .SSC files.
 * @param f the file in question.
 * @param out the Song in question. */
static void WriteGlobalTags( RageFile &f, const Song &out )
{
	f.PutLine( ssprintf( "#VERSION:%.2f;", STEPFILE_VERSION_NUMBER ) );
	f.PutLine( ssprintf( "#TITLE:%s;", SmEscape(out.m_sMainTitle).c_str() ) );
	f.PutLine( ssprintf( "#SUBTITLE:%s;", SmEscape(out.m_sSubTitle).c_str() ) );
	f.PutLine( ssprintf( "#ARTIST:%s;", SmEscape(out.m_sArtist).c_str() ) );
	f.PutLine( ssprintf( "#TITLETRANSLIT:%s;", SmEscape(out.m_sMainTitleTranslit).c_str() ) );
	f.PutLine( ssprintf( "#SUBTITLETRANSLIT:%s;", SmEscape(out.m_sSubTitleTranslit).c_str() ) );
	f.PutLine( ssprintf( "#ARTISTTRANSLIT:%s;", SmEscape(out.m_sArtistTranslit).c_str() ) );
	f.PutLine( ssprintf( "#GENRE:%s;", SmEscape(out.m_sGenre).c_str() ) );
	f.PutLine( ssprintf( "#ORIGIN:%s;", SmEscape(out.m_sOrigin).c_str() ) );
	f.PutLine( ssprintf( "#CREDIT:%s;", SmEscape(out.m_sCredit).c_str() ) );
	f.PutLine( ssprintf( "#BANNER:%s;", SmEscape(out.m_sBannerFile).c_str() ) );
	f.PutLine( ssprintf( "#BACKGROUND:%s;", SmEscape(out.m_sBackgroundFile).c_str() ) );
	f.PutLine( ssprintf( "#LYRICSPATH:%s;", SmEscape(out.m_sLyricsFile).c_str() ) );
	f.PutLine( ssprintf( "#CDTITLE:%s;", SmEscape(out.m_sCDTitleFile).c_str() ) );
	f.PutLine( ssprintf( "#MUSIC:%s;", SmEscape(out.m_sMusicFile).c_str() ) );

	{
		vector<RString> vs = out.GetInstrumentTracksToVectorString();
		if( !vs.empty() )
		{
			RString s = join( ",", vs );
			f.PutLine( "#INSTRUMENTTRACK:" + s + ";\n" );
		}
	}
	f.PutLine( ssprintf( "#OFFSET:%.3f;", out.m_SongTiming.m_fBeat0OffsetInSeconds ) );
	f.PutLine( ssprintf( "#SAMPLESTART:%.3f;", out.m_fMusicSampleStartSeconds ) );
	f.PutLine( ssprintf( "#SAMPLELENGTH:%.3f;", out.m_fMusicSampleLengthSeconds ) );

	f.Write( "#SELECTABLE:" );
	switch(out.m_SelectionDisplay)
	{
		default: ASSERT_M(0, "An invalid selectable value was found for this song!"); // fall through
		case Song::SHOW_ALWAYS:	f.Write( "YES" );		break;
		//case Song::SHOW_NONSTOP:	f.Write( "NONSTOP" );	break;
		case Song::SHOW_NEVER:		f.Write( "NO" );		break;
	}
	f.PutLine( ";" );

	switch( out.m_DisplayBPMType )
	{
	case DISPLAY_BPM_ACTUAL:
		// write nothing
		break;
	case DISPLAY_BPM_SPECIFIED:
		if( out.m_fSpecifiedBPMMin == out.m_fSpecifiedBPMMax )
			f.PutLine( ssprintf( "#DISPLAYBPM:%.3f;", out.m_fSpecifiedBPMMin ) );
		else
			f.PutLine( ssprintf( "#DISPLAYBPM:%.3f:%.3f;", out.m_fSpecifiedBPMMin, out.m_fSpecifiedBPMMax ) );
		break;
	case DISPLAY_BPM_RANDOM:
		f.PutLine( ssprintf( "#DISPLAYBPM:*;" ) );
		break;
	}

	WriteTimingTags( f, out.m_SongTiming, true );
	
	if( out.GetSpecifiedLastSecond() > 0 )
		f.PutLine( ssprintf("#LASTSECONDHINT:%.3f;", out.GetSpecifiedLastSecond()) );
	
	FOREACH_BackgroundLayer( b )
	{
		if( b==0 )
			f.Write( "#BGCHANGES:" );
		else if( out.GetBackgroundChanges(b).empty() )
			continue;	// skip
		else
			f.Write( ssprintf("#BGCHANGES%d:", b+1) );

		FOREACH_CONST( BackgroundChange, out.GetBackgroundChanges(b), bgc )
			f.PutLine( (*bgc).ToString() +"," );

		/* If there's an animation plan at all, add a dummy "-nosongbg-" tag to
		 * indicate that this file doesn't want a song BG entry added at the end.
		 * See SSCLoader::TidyUpData. This tag will be removed on load. Add it
		 * at a very high beat, so it won't cause problems if loaded in older versions. */
		if( b==0 && !out.GetBackgroundChanges(b).empty() )
			f.PutLine( "99999=-nosongbg-=1.000=0=0=0 // don't automatically add -songbackground-" );
		f.PutLine( ";" );
	}

	if( out.GetForegroundChanges().size() )
	{
		f.Write( "#FGCHANGES:" );
		FOREACH_CONST( BackgroundChange, out.GetForegroundChanges(), bgc )
		{
			f.PutLine( (*bgc).ToString() +"," );
		}
		f.PutLine( ";" );
	}

	f.Write( "#KEYSOUNDS:" );
	for( unsigned i=0; i<out.m_vsKeysoundFile.size(); i++ )
	{
		f.Write( out.m_vsKeysoundFile[i] );
		if( i != out.m_vsKeysoundFile.size()-1 )
			f.Write( "," );
	}
	f.PutLine( ";" );

	f.PutLine( ssprintf("#ATTACKS:%s;", out.GetAttackString().c_str()) );
}

/**
 * @brief Retrieve the individual batches of NoteData.
 * @param song the Song in question.
 * @param in the Steps in question.
 * @param bSavingCache a flag to see if we're saving certain cache data.
 * @return the NoteData in RString form. */
static RString GetSSCNoteData( const Song &song, const Steps &in, bool bSavingCache )
{
	vector<RString> lines;

	lines.push_back( "" );
	// Escape to prevent some clown from making a comment of "\r\n;"
	lines.push_back( ssprintf("//---------------%s - %s----------------",
		GAMEMAN->GetStepsTypeInfo(in.m_StepsType).szName, SmEscape(in.GetDescription()).c_str()) );
	lines.push_back( "#NOTEDATA:;" ); // our new separator.
	lines.push_back( ssprintf( "#CHARTNAME:%s;", SmEscape(in.GetChartName()).c_str()));
	lines.push_back( ssprintf( "#STEPSTYPE:%s;", GAMEMAN->GetStepsTypeInfo(in.m_StepsType).szName ) );
	lines.push_back( ssprintf( "#DESCRIPTION:%s;", SmEscape(in.GetDescription()).c_str() ) );
	lines.push_back( ssprintf( "#CHARTSTYLE:%s;", SmEscape(in.GetChartStyle()).c_str() ) );
	lines.push_back( ssprintf( "#DIFFICULTY:%s;", DifficultyToString(in.GetDifficulty()).c_str() ) );
	lines.push_back( ssprintf( "#METER:%d;", in.GetMeter() ) );

	vector<RString> asRadarValues;
	FOREACH_PlayerNumber( pn )
	{
		const RadarValues &rv = in.GetRadarValues( pn );
		FOREACH_ENUM( RadarCategory, rc )
			asRadarValues.push_back( ssprintf("%.3f", rv[rc]) );
	}
	lines.push_back( ssprintf( "#RADARVALUES:%s;", join(",",asRadarValues).c_str() ) );

	lines.push_back( ssprintf( "#CREDIT:%s;", SmEscape(in.GetCredit()).c_str() ) );
	
	// XXX: Is there a better way to write this?
	if (const_cast<TimingData &>(song.m_SongTiming) != in.m_Timing)
	{
		lines.push_back( ssprintf( "#OFFSET:%.3f;", in.m_Timing.m_fBeat0OffsetInSeconds ) );
		GetTimingTags( lines, in.m_Timing );
	}
	if (song.GetAttackString() != in.GetAttackString())
		lines.push_back( ssprintf("#ATTACKS:%s;", in.GetAttackString().c_str()));
	
	switch( in.GetDisplayBPM() )
	{
		case DISPLAY_BPM_ACTUAL:
			// write nothing
			break;
		case DISPLAY_BPM_SPECIFIED:
		{
			float small = in.GetMinBPM();
			float big = in.GetMaxBPM();
			if (small == big)
				lines.push_back( ssprintf( "#DISPLAYBPM:%.3f;", small ) );
			else
				lines.push_back( ssprintf( "#DISPLAYBPM:%.3f:%.3f;", small, big ) );
			break;
		}
		case DISPLAY_BPM_RANDOM:
			lines.push_back( ssprintf( "#DISPLAYBPM:*;" ) );
			break;
	}
	if (bSavingCache)
	{
		lines.push_back(ssprintf("#STEPFILENAME:%s;", in.GetFilename().c_str()));
	}
	else
	{
		RString sNoteData;
		in.GetSMNoteData( sNoteData );

		lines.push_back( song.m_vsKeysoundFile.empty() ? "#NOTES:" : "#NOTES2:" );

		TrimLeft(sNoteData);
		split( sNoteData, "\n", lines, true );
		lines.push_back( ";" );
	}
	return JoinLineList( lines );
}

bool NotesWriterSSC::Write( RString sPath, const Song &out, const vector<Steps*>& vpStepsToSave, bool bSavingCache )
{
	int flags = RageFile::WRITE;

	/* If we're not saving cache, we're saving real data, so enable SLOW_FLUSH
	 * to prevent data loss. If we're saving cache, this will slow things down
	 * too much. */
	if( !bSavingCache )
		flags |= RageFile::SLOW_FLUSH;

	RageFile f;
	if( !f.Open( sPath, flags ) )
	{
		LOG->UserLog( "Song file", sPath, "couldn't be opened for writing: %s", f.GetError().c_str() );
		return false;
	}

	WriteGlobalTags( f, out );
	
	if( bSavingCache )
	{
		f.PutLine( ssprintf( "// cache tags:" ) );
		f.PutLine( ssprintf( "#FIRSTSECOND:%.3f;", out.GetFirstSecond() ) );
		f.PutLine( ssprintf( "#LASTSECOND:%.3f;", out.GetLastSecond() ) );
		f.PutLine( ssprintf( "#SONGFILENAME:%s;", out.m_sSongFileName.c_str() ) );
		f.PutLine( ssprintf( "#HASMUSIC:%i;", out.m_bHasMusic ) );
		f.PutLine( ssprintf( "#HASBANNER:%i;", out.m_bHasBanner ) );
		f.PutLine( ssprintf( "#MUSICLENGTH:%.3f;", out.m_fMusicLengthSeconds ) );
		f.PutLine( ssprintf( "// end cache tags" ) );
	}

	// Save specified Steps to this file
	FOREACH_CONST( Steps*, vpStepsToSave, s ) 
	{
		const Steps* pSteps = *s;
		RString sTag = GetSSCNoteData( out, *pSteps, bSavingCache );
		f.PutLine( sTag );
	}
	if( f.Flush() == -1 )
		return false;

	return true;
}

void NotesWriterSSC::GetEditFileContents( const Song *pSong, const Steps *pSteps, RString &sOut )
{
	sOut = "";
	RString sDir = pSong->GetSongDir();

	// "Songs/foo/bar"; strip off "Songs/".
	vector<RString> asParts;
	split( sDir, "/", asParts );
	if( asParts.size() )
		sDir = join( "/", asParts.begin()+1, asParts.end() );
	sOut += ssprintf( "#SONG:%s;\r\n", sDir.c_str() );
	sOut += GetSSCNoteData( *pSong, *pSteps, false );
}

RString NotesWriterSSC::GetEditFileName( const Song *pSong, const Steps *pSteps )
{
	/* Try to make a unique name. This isn't guaranteed. Edit descriptions are
	 * case-sensitive, filenames on disk are usually not, and we decimate certain
	 * characters for FAT filesystems. */
	RString sFile = pSong->GetTranslitFullTitle() + " - " + pSteps->GetDescription();

	// HACK:
	if( pSteps->m_StepsType == StepsType_dance_double )
		sFile += " (doubles)";

	sFile += ".edit";

	MakeValidFilename( sFile );
	return sFile;
}

static LocalizedString DESTINATION_ALREADY_EXISTS	("NotesWriterSSC", "Error renaming file.  Destination file '%s' already exists.");
static LocalizedString ERROR_WRITING_FILE		("NotesWriterSSC", "Error writing file '%s'.");
bool NotesWriterSSC::WriteEditFileToMachine( const Song *pSong, Steps *pSteps, RString &sErrorOut )
{
	RString sDir = PROFILEMAN->GetProfileDir( ProfileSlot_Machine ) + EDIT_STEPS_SUBDIR;

	RString sPath = sDir + GetEditFileName(pSong,pSteps);

	// Check to make sure that we're not clobering an existing file before opening.
	bool bFileNameChanging = 
		pSteps->GetSavedToDisk()  && 
		pSteps->GetFilename() != sPath;
	if( bFileNameChanging  &&  DoesFileExist(sPath) )
	{
		sErrorOut = ssprintf( DESTINATION_ALREADY_EXISTS.GetValue(), sPath.c_str() );
		return false;
	}

	RageFile f;
	if( !f.Open(sPath, RageFile::WRITE | RageFile::SLOW_FLUSH) )
	{
		sErrorOut = ssprintf( ERROR_WRITING_FILE.GetValue(), sPath.c_str() );
		return false;
	}

	RString sTag;
	GetEditFileContents( pSong, pSteps, sTag );
	if( f.PutLine(sTag) == -1 || f.Flush() == -1 )
	{
		sErrorOut = ssprintf( ERROR_WRITING_FILE.GetValue(), sPath.c_str() );
		return false;
	}

	/* If the file name of the edit has changed since the last save, then delete the old
	 * file after saving the new one. If we delete it first, then we'll lose data on error. */

	if( bFileNameChanging )
		FILEMAN->Remove( pSteps->GetFilename() );
	pSteps->SetFilename( sPath );

	return true;
}

/*
 * (c) 2011 Jason Felds
 * All rights reserved.
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, and/or sell copies of the Software, and to permit persons to
 * whom the Software is furnished to do so, provided that the above
 * copyright notice(s) and this permission notice appear in all copies of
 * the Software and that both the above copyright notice(s) and this
 * permission notice appear in supporting documentation.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF
 * THIRD PARTY RIGHTS. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR HOLDERS
 * INCLUDED IN THIS NOTICE BE LIABLE FOR ANY CLAIM, OR ANY SPECIAL INDIRECT
 * OR CONSEQUENTIAL DAMAGES, OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
 * OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */
