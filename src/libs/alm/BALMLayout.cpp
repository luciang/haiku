/*
 * Copyright 2007-2008, Christof Lutteroth, lutteroth@cs.auckland.ac.nz
 * Copyright 2007-2008, James Kim, jkim202@ec.auckland.ac.nz
 * Distributed under the terms of the MIT License.
 */

#include "BALMLayout.h"
#include "Area.h"
#include "Column.h"
#include "ResultType.h"
#include "Row.h"
#include "XTab.h"
#include "YTab.h"

#include <math.h>		// for floor


/**
 * Constructor.
 * Creates new layout engine.
 */
BALMLayout::BALMLayout() 
	: BLayout(),
	LinearSpec()
{
	fLayoutStyle = FIT_TO_SIZE;
	fActivated = true;
	
	fAreas = new BList(1);
	fLeft = new XTab(this);
	fRight = new XTab(this);
	fTop = new YTab(this);
	fBottom = new YTab(this);
	
	// the Left tab is always at x-position 0, and the Top tab is always at y-position 0
	fLeft->SetRange(0, 0);
	fTop->SetRange(0, 0);
	
	// cached layout values
	// need to be invalidated whenever the layout specification is changed
	fMinSize = Area::kUndefinedSize;
	fMaxSize = Area::kUndefinedSize;
	fPreferredSize = Area::kUndefinedSize;
	
	fPerformancePath = NULL;
}


/**
 * Solves the layout.
 */
void
BALMLayout::SolveLayout()
{
	// if autoPrefContentSize is set on an area, readjust its prefContentSize and 
	// rigidity settings
	int32 sizeAreas = fAreas->CountItems();
	Area* currentArea;
	for (int32 i = 0; i < sizeAreas; i++) {
		currentArea = (Area*)fAreas->ItemAt(i);
		if (currentArea->AutoPrefContentSize())
			currentArea->SetDefaultPrefContentSize();
	}
	
	// try to solve the layout until the result is OPTIMAL or INFEASIBLE, maximally 
	// 15 tries sometimes the solving algorithm encounters numerical problems 
	// (NUMFAILURE), and repeating the solving often helps to overcome them
	BFile* file = new BFile(fPerformancePath, 
				B_READ_WRITE | B_CREATE_FILE | B_OPEN_AT_END);
	
	ResultType result;
	for (int32 tries = 0; tries < 15; tries++) {
		result = Solve();
		if (fPerformancePath != NULL) {
			char buffer [100];
			file->Write(buffer, sprintf(buffer, "%d\t%fms\t#vars=%ld\t#constraints=%ld\n", 
					result, SolvingTime(), Variables()->CountItems(), 
					Constraints()->CountItems()));
		}
		if (result == OPTIMAL || result == INFEASIBLE) break;
	}
	delete file;
}


/**
 * Adds a new x-tab to the specification.
 * 
 * @return the new x-tab
 */
XTab*
BALMLayout::AddXTab()
{
	return new XTab(this);
}
	

/**
 * Adds a new y-tab to the specification.
 * 
 * @return the new y-tab
 */
YTab*
BALMLayout::AddYTab()
{
	return new YTab(this);
}


/**
 * Adds a new row to the specification.
 * 
 * @return the new row
 */
Row*
BALMLayout::AddRow()
{
	return new Row(this);
}


/**
 * Adds a new row to the specification that is glued to the given y-tabs.
 * 
 * @param top
 * @param bottom
 * @return the new row
 */
Row*
BALMLayout::AddRow(YTab* top, YTab* bottom)
{
	Row* row = new Row(this);
	if (top != NULL) row->Constraints()->AddItem(row->Top()->IsEqual(top));
	if (bottom != NULL) row->Constraints()->AddItem(row->Bottom()->IsEqual(bottom));
	return row;
}


/**
 * Adds a new column to the specification.
 * 
 * @return the new column
 */
Column*
BALMLayout::AddColumn()
{
	return new Column(this);
}


/**
 * Adds a new column to the specification that is glued to the given x-tabs.
 * 
 * @param left
 * @param right
 * @return the new column
 */
Column*
BALMLayout::AddColumn(XTab* left, XTab* right)
{
	Column* column = new Column(this);
	if (left != NULL) column->Constraints()->AddItem(column->Left()->IsEqual(left));
	if (right != NULL) column->Constraints()->AddItem(column->Right()->IsEqual(right));
	return column;
}


/**
 * Adds a new area to the specification, setting only the necessary minimum size constraints.
 * 
 * @param left				left border
 * @param top				top border
 * @param right			right border
 * @param bottom			bottom border
 * @param content			the control which is the area content
 * @param minContentSize	minimum content size
 * @return the new area
 */
Area*
BALMLayout::AddArea(XTab* left, YTab* top, XTab* right, YTab* bottom,
	BView* content, BSize minContentSize)
{			
	InvalidateLayout();
	if (content != NULL)
		View()->AddChild(content);
	Area* area = new Area(this, left, top, right, bottom, content, minContentSize);
	fAreas->AddItem(area);
	return area;
}


/**
 * Adds a new area to the specification, setting only the necessary minimum size constraints.
 * 
 * @param row				the row that defines the top and bottom border
 * @param column			the column that defines the left and right border
 * @param content			the control which is the area content
 * @param minContentSize	minimum content size
 * @return the new area
 */
Area*
BALMLayout::AddArea(Row* row, Column* column, BView* content,
	BSize minContentSize)
{	
	InvalidateLayout();
	if (content != NULL)
		View()->AddChild(content);
	Area* area = new Area(this, row, column, content, minContentSize);
	fAreas->AddItem(area);
	return area;
}


/**
 * Adds a new area to the specification, automatically setting preferred size constraints.
 * 
 * @param left			left border
 * @param top			top border
 * @param right		right border
 * @param bottom		bottom border
 * @param content		the control which is the area content
 * @return the new area
 */
Area*
BALMLayout::AddArea(XTab* left, YTab* top, XTab* right, YTab* bottom,
	BView* content)
{	
	InvalidateLayout();
	if (content != NULL)
		View()->AddChild(content);
	Area* area = new Area(this, left, top, right, bottom, content, BSize(0, 0));
	area->SetDefaultPrefContentSize();
	area->SetAutoPrefContentSize(false);
	fAreas->AddItem(area);
	return area;
}


/**
 * Adds a new area to the specification, automatically setting preferred size constraints.
 * 
 * @param row			the row that defines the top and bottom border
 * @param column		the column that defines the left and right border
 * @param content		the control which is the area content
 * @return the new area    
 */
Area*
BALMLayout::AddArea(Row* row, Column* column, BView* content)
{
	InvalidateLayout();
	if (content != NULL)
		View()->AddChild(content);
	Area* area = new Area(this, row, column, content, BSize(0, 0));
	area->SetDefaultPrefContentSize();
	area->SetAutoPrefContentSize(false);
	fAreas->AddItem(area);
	return area;
}


/**
 * Finds the area that contains the given control.
 * 
 * @param control	the control to look for
 * @return the area that contains the control
 */
Area*
BALMLayout::AreaOf(BView* control)
{
	Area* area;
	for (int32 i = 0; i < fAreas->CountItems(); i++) {
		area = (Area*)fAreas->ItemAt(i);
		if (area->Content() == control) 
			return area;
	}
	return NULL;
}


/**
 * Gets the ares.
 *
 * @return the areas
 */
BList*
BALMLayout::Areas() const
{
	return fAreas;
}


/**
 * Gets the left variable.
 */
XTab*
BALMLayout::Left() const
{
	return fLeft;
}


/**
 * Gets the right variable.
 */
XTab*
BALMLayout::Right() const
{
	return fRight;
}


/**
 * Gets the top variable.
 */
YTab*
BALMLayout::Top() const
{
	return fTop;
}


/**
 * Gets the bottom variable.
 */
YTab*
BALMLayout::Bottom() const
{
	return fBottom;
}


/**
 * Reverse engineers a GUI and recovers an ALM specification.
 * @param parent	the parent container of the GUI
 */
void
BALMLayout::RecoverLayout(BView* parent) {}	// Still working on it.


/**
 * Gets the current layout style.
 */
LayoutStyleType
BALMLayout::LayoutStyle() const
{
	return fLayoutStyle;
}


/**
 * Sets the current layout style.
 */
void
BALMLayout::SetLayoutStyle(LayoutStyleType style)
{
	fLayoutStyle = style;
}


/**
 * Adds view to layout.
 */
BLayoutItem*
BALMLayout::AddView(BView* child)
{
	return NULL;
}


/**
 * Adds view to layout.
 */
BLayoutItem*
BALMLayout::AddView(int32 index, BView* child)
{
return NULL;
}


/**
 * Adds item to layout.
 */
bool
BALMLayout::AddItem(BLayoutItem* item)
{
	return false;
}


/**
 * Adds item to layout.
 */
bool
BALMLayout::AddItem(int32 index, BLayoutItem* item)
{
	return false;
}


/**
 * Removes view from layout.
 */
bool
BALMLayout::RemoveView(BView* child)
{
	return false;
}


/**
 * Removes item from layout.
 */
bool
BALMLayout::RemoveItem(BLayoutItem* item)
{
	return false;
}


/**
 * Removes item from layout.
 */
BLayoutItem*
BALMLayout::RemoveItem(int32 index)
{
	return NULL;
}


/**
 * Sets and gets minimum size.
 */
BSize BALMLayout::MinSize() {
	if (fMinSize == Area::kUndefinedSize)
		fMinSize = CalculateMinSize();
	return fMinSize;
}


/**
 * Sets and gets maximum size.
 */
BSize
BALMLayout::MaxSize()
{
	if (fMaxSize == Area::kUndefinedSize)
		fMaxSize = CalculateMaxSize();
	return fMaxSize;
}


/**
 * Sets and gets preferred size.
 */
BSize
BALMLayout::PreferredSize()
{
	if (fPreferredSize == Area::kUndefinedSize)
		fPreferredSize = CalculatePreferredSize();
	return fPreferredSize;
}


/**
 * Gets the alignment.
 */
BAlignment
BALMLayout::Alignment()
{
	BAlignment alignment;
	alignment.SetHorizontal(B_ALIGN_HORIZONTAL_CENTER);
	alignment.SetVertical(B_ALIGN_VERTICAL_CENTER);
	return alignment;
}


/**
 * Gets whether the height of the layout depends on its width.
 */
bool
BALMLayout::HasHeightForWidth()
{
	return false;
}


/**
 * Sets whether the height of the layout depends on its width.
 */
void
BALMLayout::GetHeightForWidth(float width, float* min, float* max, float* preferred) {}


/**
 * Invalidates the layout.
 * Resets minimum/maximum/preferred size.
 */
void
BALMLayout::InvalidateLayout()
{
	fMinSize = Area::kUndefinedSize;
	fMaxSize = Area::kUndefinedSize;
	fPreferredSize = Area::kUndefinedSize;
}


/**
 * Calculate and set the layout.
 * If no layout specification is given, a specification is reverse engineered automatically.
 */
void
BALMLayout::LayoutView()
{
	// make sure that layout events occuring during layout are ignored
	// i.e. activated is set to false during layout caluclation
	if (!fActivated)
		return;
	fActivated = false;
	
	if (View() == NULL)
		return;
	
	// reverse engineer a layout specification if none was given
	//~ if (this == NULL) RecoverLayout(View());
	
	// if the layout engine is set to fit the GUI to the given size,
	// then the given size is enforced by setting absolute positions for Right and Bottom
	if (fLayoutStyle == FIT_TO_SIZE) {
		Right()->SetRange(View()->Bounds().Width(), View()->Bounds().Width());
		Bottom()->SetRange(View()->Bounds().Height(), View()->Bounds().Height());
	}
	
	SolveLayout();
	
	// if new layout is infasible, use previous layout
	if (Result() == INFEASIBLE) {
		fActivated = true; // now layout calculation is allowed to run again
		return;
	}
	
	if (Result() != OPTIMAL) {
		Save("failed-layout.txt");
		printf("Could not solve the layout specification (%d). ", Result());
		printf("Saved specification in file failed-layout.txt\n");
	}
	
	// change the size of the GUI according to the calculated size 
	// if the layout engine was configured to do so
	if (fLayoutStyle == ADJUST_SIZE) {
		View()->ResizeTo(floor(Right()->Value() - Left()->Value() + 0.5),
				floor(Bottom()->Value() - Top()->Value() + 0.5));
	}
	
	// set the calculated positions and sizes for every area
	for (int32 i = 0; i < Areas()->CountItems(); i++)
		((Area*)Areas()->ItemAt(i))->DoLayout();
	
	fActivated = true;
}


/**
 * Gets the path of the performance log file.
 *
 * @return the path of the performance log file
 */
char*
BALMLayout::PerformancePath() const
{
	return fPerformancePath;
}


/**
 * Sets the path of the performance log file.
 *
 * @param path	the path of the performance log file
 */
void
BALMLayout::SetPerformancePath(char* path)
{
	fPerformancePath = path;
}


/**
 * Caculates the miminum size.
 */
BSize
BALMLayout::CalculateMinSize()
{
	BList* oldObjFunction = ObjFunction();
	BList* newObjFunction = new BList(2);
	newObjFunction->AddItem(new Summand(1.0, fRight));
	newObjFunction->AddItem(new Summand(1.0, fBottom));
	SetObjFunction(newObjFunction);
	SolveLayout();
	SetObjFunction(oldObjFunction);
	UpdateObjFunction();
	delete (Summand*)newObjFunction->ItemAt(0);
	delete (Summand*)newObjFunction->ItemAt(1);
	delete newObjFunction;
	
	if (Result() == UNBOUNDED)
		return Area::kMinSize;
	if (Result() != OPTIMAL) {
		Save("failed-layout.txt");
		printf("Could not solve the layout specification (%d). Saved specification in file failed-layout.txt", Result());
	}
	
	return BSize(Right()->Value() - Left()->Value(), Bottom()->Value() - Top()->Value());
}


/**
 * Caculates the maximum size.
 */
BSize
BALMLayout::CalculateMaxSize()
{
	BList* oldObjFunction = ObjFunction();
	BList* newObjFunction = new BList(2);
	newObjFunction->AddItem(new Summand(-1.0, fRight));
	newObjFunction->AddItem(new Summand(-1.0, fBottom));
	SetObjFunction(newObjFunction);
	SolveLayout();
	SetObjFunction(oldObjFunction);
	UpdateObjFunction();
	delete (Summand*)newObjFunction->ItemAt(0);
	delete (Summand*)newObjFunction->ItemAt(1);
	delete newObjFunction;
	
	if (Result() == UNBOUNDED)
		return Area::kMaxSize;
	if (Result() != OPTIMAL) {
		Save("failed-layout.txt");
		printf("Could not solve the layout specification (%d). Saved specification in file failed-layout.txt", Result());
	}
	
	return BSize(Right()->Value() - Left()->Value(), Bottom()->Value() - Top()->Value());
}


/**
 * Caculates the preferred size.
 */
BSize
BALMLayout::CalculatePreferredSize()
{
	SolveLayout();
	if (Result() != OPTIMAL) {
		Save("failed-layout.txt");
		printf("Could not solve the layout specification (%d). Saved specification in file failed-layout.txt", Result());
	}
	
	return BSize(Right()->Value() - Left()->Value(), Bottom()->Value() - Top()->Value());
}

