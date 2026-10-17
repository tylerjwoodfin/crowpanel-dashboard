#import <Cocoa/Cocoa.h>
#import <EventKit/EventKit.h>
#import <stdio.h>

// Prints {"items":[...]} for incomplete reminders on the list named Grocery.
// Does not complete, edit, or delete anything.

static const char *outputPath = NULL;

static void fail(NSString *message) {
    fprintf(stderr, "%s\n", message.UTF8String);
    exit(1);
}

static void emit(NSData *body) {
    if (outputPath == NULL) {
        fwrite(body.bytes, 1, body.length, stdout);
        fflush(stdout);
        return;
    }
    NSString *path = [NSString stringWithUTF8String:outputPath];
    if (![body writeToFile:path atomically:YES]) {
        fail([NSString stringWithFormat:@"could not write %@", path]);
    }
}

int main(int argc, char **argv) {
    @autoreleasepool {
        if (argc > 1) outputPath = argv[1];
        NSApplication *app = [NSApplication sharedApplication];
        [app setActivationPolicy:NSApplicationActivationPolicyAccessory];
        EKEventStore *store = [[EKEventStore alloc] init];
        [store requestFullAccessToRemindersWithCompletion:^(BOOL granted, NSError *error) {
            if (!granted) {
                fail(error.localizedDescription ?: @"Reminders access was not granted");
            }

            EKCalendar *grocery = nil;
            for (EKCalendar *calendar in [store calendarsForEntityType:EKEntityTypeReminder]) {
                if ([calendar.title isEqualToString:@"Grocery"]) {
                    grocery = calendar;
                    break;
                }
            }
            if (grocery == nil) {
                fail(@"Reminders list \"Grocery\" was not found");
            }

            NSPredicate *predicate = [store predicateForIncompleteRemindersWithDueDateStarting:nil
                                                                                         ending:nil
                                                                                      calendars:@[grocery]];
            [store fetchRemindersMatchingPredicate:predicate completion:^(NSArray<EKReminder *> *reminders) {
                NSArray<EKReminder *> *sorted = [reminders sortedArrayUsingComparator:^NSComparisonResult(EKReminder *a, EKReminder *b) {
                    NSDate *left = a.creationDate ?: NSDate.distantPast;
                    NSDate *right = b.creationDate ?: NSDate.distantPast;
                    return [left compare:right];
                }];

                NSMutableArray<NSString *> *titles = [NSMutableArray array];
                NSMutableSet<NSString *> *seen = [NSMutableSet set];
                for (EKReminder *reminder in sorted) {
                    if (reminder.completed) continue;
                    NSString *title = [reminder.title stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];
                    if (title.length == 0 || [seen containsObject:title]) continue;
                    [seen addObject:title];
                    [titles addObject:title];
                }

                NSData *body = [NSJSONSerialization dataWithJSONObject:@{@"items": titles} options:0 error:nil];
                emit(body);
                exit(0);
            }];
        }];
        [app run];
    }
    return 0;
}
