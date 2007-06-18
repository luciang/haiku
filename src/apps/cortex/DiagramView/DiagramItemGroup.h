// DiagramItemGroup.h (Cortex/DiagramView)
//
// * HISTORY
//   c.lenz		28sep99		Begun
//
#ifndef DIAGRAM_ITEM_GROUP_H
#define DIAGRAM_ITEM_GROUP_H


#include "cortex_defs.h"

#include "DiagramItem.h"

#include <List.h>
#include <Point.h>



__BEGIN_CORTEX_NAMESPACE


class DiagramItemGroup {
	public:
		DiagramItemGroup(uint32 acceptedTypes, bool multiSelection = true);
		virtual ~DiagramItemGroup();

		// hook function
		// is called whenever the current selection has changed
		virtual void SelectionChanged()
		{
		}

		// item accessors

		uint32 CountItems(uint32 whichType = DiagramItem::M_ANY) const;
		DiagramItem* ItemAt(uint32 index,
			uint32 whichType = DiagramItem::M_ANY) const;
		DiagramItem* ItemUnder(BPoint point);

		// item operations

		virtual bool AddItem(DiagramItem* item);
		bool RemoveItem(DiagramItem* item);
		void SortItems(uint32 itemType,
			int (*compareFunc)(const void *, const void *));
		void DrawItems(BRect updateRect, uint32 whichType = DiagramItem::M_ANY,
			BRegion *updateRegion = 0);
		bool GetClippingAbove(DiagramItem* which, BRegion* outRegion);

		// selection accessors

		uint32 SelectedType() const;
		uint32 CountSelectedItems() const;
		DiagramItem* SelectedItemAt(uint32 index) const;

		// returns the ability of the group to handle multiple selections
		// as set in the constructor
		bool MultipleSelection() const
		{
			return fMultiSelection;
		}						

		// selection related operations

		bool SelectItem(DiagramItem* which, bool deselectOthers = true);
		bool DeselectItem(DiagramItem *which);
		void SortSelectedItems(int (*compareFunc)(const void *, const void *));
		bool SelectAll(uint32 itemType = DiagramItem::M_ANY);
		bool DeselectAll(uint32 itemType = DiagramItem::M_ANY);
		void DragSelectionBy(float x, float y, BRegion *updateRegion);
		void RemoveSelection();

		// alignment related

		// set/get the 'item alignment' grid size; this is used when
		// items are being dragged and inserted
		void SetItemAlignment(float horizontal, float vertical)
		{
			fItemAlignment.Set(horizontal, vertical);
		}

		void GetItemAlignment(float *horizontal, float *vertical);
		void Align(float *x, float *y) const;
		BPoint Align(BPoint point) const;

	protected: // accessors

		/*!	Returns a pointer to the last item returned by the ItemUnder()
			function; this can be used by deriving classes to implement a
			transit system
		*/
		DiagramItem* _LastItemUnder()
		{
			return fLastItemUnder;
		}

		void _ResetItemUnder()
		{
			fLastItemUnder = 0;
		}

	private: // data members

		// pointers to the item-lists (one list for each item type)
		BList* 		fBoxes;
		BList* 		fWires;
		BList* 		fEndPoints;

		BList*		fSelection;
			// pointer to the list containing the current selection

		uint32		fTypes;
			// the DiagramItem type(s) of items this group will accept

		BPoint		fItemAlignment;
			// specifies the "grid"-size for the frames of items		

		bool		fMultiSelection;
			// can multiple items be selected at once ?

		DiagramItem* fLastItemUnder;
			// cached pointer to the item that was found in ItemUnder()
};

__END_CORTEX_NAMESPACE
#endif	// DIAGRAM_ITEM_GROUP_H
