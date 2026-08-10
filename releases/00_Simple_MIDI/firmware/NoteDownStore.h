#ifndef NOTEDOWNSTORE_H
#define NOTEDOWNSTORE_H

/*
Given note-on and note-off events, returns most recently pressed note that is still held down.

All operations O(1) time complexity; based on linked list based on fixed array of 128 MIDI notes.
*/
class NoteDownStore
{
private:
	constexpr static unsigned NUM_NOTES = 128;

	class NoteDown
	{
	public:
		int8_t prev, next, velocity;
		NoteDown()
		{
			prev = -1;
			next = -1;
			velocity = -1;
		}
	};

public:
	NoteDownStore()
	{
		lastNoteDown = -1;
	}

	void NoteOn(int8_t val, int8_t velocity)
	{
		// Should never get a note on for a note that is already down.
		// If we do, ignore it
		if (val == lastNoteDown || notesDown[val].prev != -1 || notesDown[val].next != -1)
			return;

		notesDown[val].velocity = velocity;
		// If it's not the first note, link this new note to a previous note
		if (lastNoteDown != -1)
		{
			notesDown[lastNoteDown].next = val;
			notesDown[val].prev = lastNoteDown;
		}

		lastNoteDown = val;
	}

	// Called with a MIDI note number 0-127 inclusive
	void NoteOff(int8_t val)
	{
		// Invalid MIDI note number? Should never happen,
		// but bail out here before buffer overrun occurs
		if (val < 0)
			return;

		// If note released is the most recently pressed...
		if (val == lastNoteDown)
		{
			// Move lastNoteDown to previously pressed note, and clear the released note's prev pointer
			// If this was the only note down, lastNoteDown is now -1 (no notes down)
			lastNoteDown = notesDown[val].prev;
			notesDown[val].prev = -1;
			notesDown[val].velocity = -1;

			// If this wasn't the only note down...
			if (lastNoteDown != -1)
			{
				// clear the pointer pointing to the node just released.
				notesDown[lastNoteDown].next = -1;
			}
		}
		else // this was not the most recently pressed note
		{
			if (notesDown[val].next != -1)
			{
				notesDown[notesDown[val].next].prev = notesDown[val].prev;
			}
			else
			{
				// val is not in the list (duplicate note-off); nothing to unlink
				return;
			}

			// if this was not the first note down
			if (notesDown[val].prev != -1)
			{
				notesDown[notesDown[val].prev].next = notesDown[val].next;
			}

			notesDown[val].prev = -1;
			notesDown[val].next = -1;
			notesDown[val].velocity = -1;
		}
	}
	int8_t LastNoteDown() const
	{
		return lastNoteDown;
	}
	int8_t LastNoteDownVelocity() const
	{
		if (lastNoteDown < 0) return -1;
		return notesDown[lastNoteDown].velocity;
	}

private:
	NoteDown notesDown[NUM_NOTES];
	volatile int8_t lastNoteDown;
};


#endif
