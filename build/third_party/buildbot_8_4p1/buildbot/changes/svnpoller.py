# This file is part of Buildbot.  Buildbot is free software: you can
# redistribute it and/or modify it under the terms of the GNU General Public
# License as published by the Free Software Foundation, version 2.
#
# This program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
# FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
# details.
#
# You should have received a copy of the GNU General Public License along with
# this program; if not, write to the Free Software Foundation, Inc., 51
# Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
#
# Copyright Buildbot Team Members


# Based on the work of Dave Peticolas for the P4poll
# Changed to svn (using xml.dom.minidom) by Niklaus Giger
# Hacked beyond recognition by Brian Warner

from twisted.python import log
from twisted.internet import defer, utils

from buildbot import util
from buildbot.changes import base

import xml.dom.minidom
import os, urllib

# these split_file_* functions are available for use as values to the
# split_file= argument.
def split_file_alwaystrunk(path):
    return (None, path)

def split_file_branches(path):
    # turn trunk/subdir/file.c into (None, "subdir/file.c")
    # and branches/1.5.x/subdir/file.c into ("branches/1.5.x", "subdir/file.c")
    pieces = path.split('/')
    if pieces[0] == 'trunk':
        return (None, '/'.join(pieces[1:]))
    elif pieces[0] == 'branches':
        return ('/'.join(pieces[0:2]), '/'.join(pieces[2:]))
    else:
        return None


class SVNPoller(base.PollingChangeSource, util.ComparableMixin):
    """
    Poll a Subversion repository for changes and submit them to the change
    master.
    """

    compare_attrs = ["svnurl", "split_file",
                     "svnuser", "svnpasswd",
                     "pollInterval", "histmax",
                     "svnbin", "category", "cachepath"]

    parent = None # filled in when we're added
    last_change = None
    loop = None

    def __init__(self, svnurl, split_file=None,
                 svnuser=None, svnpasswd=None,
                 pollInterval=10*60, histmax=100,
                 svnbin='svn', revlinktmpl='', category=None, 
                 project='', cachepath=None, pollinterval=-2):
        # for backward compatibility; the parameter used to be spelled with 'i'
        if pollinterval != -2:
            pollInterval = pollinterval

        if svnurl.endswith("/"):
            svnurl = svnurl[:-1] # strip the trailing slash
        self.svnurl = svnurl
        self.split_file = split_file or split_file_alwaystrunk
        self.svnuser = svnuser
        self.svnpasswd = svnpasswd

        self.revlinktmpl = revlinktmpl

        self.environ = os.environ.copy() # include environment variables
                                         # required for ssh-agent auth

        self.svnbin = svnbin
        self.pollInterval = pollInterval
        self.histmax = histmax
        self._prefix = None
        self.category = category
        self.project = project

        self.cachepath = cachepath
        if self.cachepath and os.path.exists(self.cachepath):
            try:
                f = open(self.cachepath, "r")
                self.last_change = int(f.read().strip())
                log.msg("SVNPoller(%s) setting last_change to %s" % (self.svnurl, self.last_change))
                f.close()
                # try writing it, too
                f = open(self.cachepath, "w")
                f.write(str(self.last_change))
                f.close()
            except:
                self.cachepath = None
                log.msg(("SVNPoller(%s) cache file corrupt or unwriteable; " +
                        "skipping and not using") % self.svnurl)
                log.err()

    def describe(self):
        return "SVNPoller watching %s" % self.svnurl

    def poll(self):
        # Our return value is only used for unit testing.

        # we need to figure out the repository root, so we can figure out
        # repository-relative pathnames later. Each SVNURL is in the form
        # (ROOT)/(PROJECT)/(BRANCH)/(FILEPATH), where (ROOT) is something
        # like svn://svn.twistedmatrix.com/svn/Twisted (i.e. there is a
        # physical repository at /svn/Twisted on that host), (PROJECT) is
        # something like Projects/Twisted (i.e. within the repository's
        # internal namespace, everything under Projects/Twisted/ has
        # something to do with Twisted, but these directory names do not
        # actually appear on the repository host), (BRANCH) is something like
        # "trunk" or "branches/2.0.x", and (FILEPATH) is a tree-relative
        # filename like "twisted/internet/defer.py".

        # our self.svnurl attribute contains (ROOT)/(PROJECT) combined
        # together in a way that we can't separate without svn's help. If the
        # user is not using the split_file= argument, then self.svnurl might
        # be (ROOT)/(PROJECT)/(BRANCH) . In any case, the filenames we will
        # get back from 'svn log' will be of the form
        # (PROJECT)/(BRANCH)/(FILEPATH), but we want to be able to remove
        # that (PROJECT) prefix from them. To do this without requiring the
        # user to tell us how svnurl is split into ROOT and PROJECT, we do an
        # 'svn info --xml' command at startup. This command will include a
        # <root> element that tells us ROOT. We then strip this prefix from
        # self.svnurl to determine PROJECT, and then later we strip the
        # PROJECT prefix from the filenames reported by 'svn log --xml' to
        # get a (BRANCH)/(FILEPATH) that can be passed to split_file() to
        # turn into separate BRANCH and FILEPATH values.

        # whew.

        if self.project:
            log.msg("SVNPoller polling " + self.project)
        else:
            log.msg("SVNPoller polling")

        d = defer.succeed(None)
        if not self._prefix:
            d.addCallback(lambda _ : self.get_prefix())
            def set_prefix(prefix):
                self._prefix = prefix
            d.addCallback(set_prefix)

        d.addCallback(self.get_logs)
        d.addCallback(self.parse_logs)
        d.addCallback(self.get_new_logentries)
        d.addCallback(self.create_changes)
        d.addCallback(self.submit_changes)
        d.addCallback(self.finished_ok)
        d.addErrback(log.err, 'error in SVNPoller while polling') # eat errors
        return d

    def getProcessOutput(self, args):
        # this exists so we can override it during the unit tests
        d = utils.getProcessOutput(self.svnbin, args, self.environ)
        return d

    def get_prefix(self):
        args = ["info", "--xml", "--non-interactive", self.svnurl]
        if self.svnuser:
            args.extend(["--username=%s" % self.svnuser])
        if self.svnpasswd:
            args.extend(["--password=%s" % self.svnpasswd])
        d = self.getProcessOutput(args)
        def determine_prefix(output):
            try:
                doc = xml.dom.minidom.parseString(output)
            except xml.parsers.expat.ExpatError:
                log.msg("SVNPoller._determine_prefix_2: ExpatError in '%s'"
                        % output)
                raise
            rootnodes = doc.getElementsByTagName("root")
            if not rootnodes:
                # this happens if the URL we gave was already the root. In this
                # case, our prefix is empty.
                self._prefix = ""
                return self._prefix
            rootnode = rootnodes[0]
            root = "".join([c.data for c in rootnode.childNodes])
            # root will be a unicode string
            assert self.svnurl.startswith(root), \
                    ("svnurl='%s' doesn't start with <root>='%s'" %
                    (self.svnurl, root))
            prefix = self.svnurl[len(root):]
            if prefix.startswith("/"):
                prefix = prefix[1:]
            log.msg("SVNPoller: svnurl=%s, root=%s, so prefix=%s" %
                    (self.svnurl, root, prefix))
            return prefix
        d.addCallback(determine_prefix)
        return d

    def get_logs(self, _):
        args = []
        args.extend(["log", "--xml", "--verbose", "--non-interactive"])
        if self.svnuser:
            args.extend(["--username=%s" % self.svnuser])
        if self.svnpasswd:
            args.extend(["--password=%s" % self.svnpasswd])
        args.extend(["--limit=%d" % (self.histmax), self.svnurl])
        d = self.getProcessOutput(args)
        return d

    def parse_logs(self, output):
        # parse the XML output, return a list of <logentry> nodes
        try:
            doc = xml.dom.minidom.parseString(output)
        except xml.parsers.expat.ExpatError:
            log.msg("SVNPoller.parse_logs: ExpatError in '%s'" % output)
            raise
        logentries = doc.getElementsByTagName("logentry")
        return logentries


    def get_new_logentries(self, logentries):
        last_change = old_last_change = self.last_change

        # given a list of logentries, calculate new_last_change, and
        # new_logentries, where new_logentries contains only the ones after
        # last_change

        new_last_change = None
        new_logentries = []
        if logentries:
            new_last_change = int(logentries[0].getAttribute("revision"))

            if last_change is None:
                # if this is the first time we've been run, ignore any changes
                # that occurred before now. This prevents a build at every
                # startup.
                log.msg('svnPoller: starting at change %s' % new_last_change)
            elif last_change == new_last_change:
                # an unmodified repository will hit this case
                log.msg('svnPoller: no changes')
            else:
                for el in logentries:
                    if last_change == int(el.getAttribute("revision")):
                        break
                    new_logentries.append(el)
                new_logentries.reverse() # return oldest first

        # If the newest commit's author is chrome-bot, skip this commit.  This
        # is a guard to ensure that we don't poll on our mirror while it could
        # be mid-sync.  In that case, the author data could be wrong and would
        # look like it was a commit by chrome-bot@google.com.  A downside: the
        # chrome-bot account may have a legitimate commit.  This should not
        # happen generally, so we're okay waiting to see it until there's a
        # later commit with a non-chrome-bot author.
        debug_change = []
        for logentry in new_logentries:
          rev = int(logentry.getAttribute("revision"))
          author = self._get_text(logentry, "author")
          debug_change.append([rev, author])
        log.msg('svnPoller: debug_change: %r' % debug_change)
        if len(new_logentries) > 0:
          newest_rev_author = self._get_text(new_logentries[-1], "author")
          if newest_rev_author == 'chrome-bot@google.com':
            new_logentries.pop(-1)
            new_last_change = int(logentries[1].getAttribute("revision"))

        self.last_change = new_last_change
        log.msg('svnPoller: _process_changes %s .. %s' %
                (old_last_change, new_last_change))
        return new_logentries


    def _get_text(self, element, tag_name):
        try:
            child_nodes = element.getElementsByTagName(tag_name)[0].childNodes
            text = "".join([t.data for t in child_nodes])
        except:
            text = "<unknown>"
        return text

    def _transform_path(self, path):
        assert path.startswith(self._prefix), \
                ("filepath '%s' should start with prefix '%s'" %
                (path, self._prefix))
        relative_path = path[len(self._prefix):]
        if relative_path.startswith("/"):
            relative_path = relative_path[1:]
        where = self.split_file(relative_path)
        # 'where' is either None or (branch, final_path)
        return where

    def create_changes(self, new_logentries):
        changes = []

        for el in new_logentries:
            revision = str(el.getAttribute("revision"))

            revlink=''

            if self.revlinktmpl:
                if revision:
                    revlink = self.revlinktmpl % urllib.quote_plus(revision)

            log.msg("Adding change revision %s" % (revision,))
            author   = self._get_text(el, "author")
            comments = self._get_text(el, "msg")
            # there is a "date" field, but it provides localtime in the
            # repository's timezone, whereas we care about buildmaster's
            # localtime (since this will get used to position the boxes on
            # the Waterfall display, etc). So ignore the date field, and
            # addChange will fill in with the current time
            branches = {}
            try:
                pathlist = el.getElementsByTagName("paths")[0]
            except IndexError: # weird, we got an empty revision
                log.msg("ignoring commit with no paths")
                continue

            for p in pathlist.getElementsByTagName("path"):
                action = p.getAttribute("action")
                path = "".join([t.data for t in p.childNodes])
                # the rest of buildbot is certaily not yet ready to handle
                # unicode filenames, because they get put in RemoteCommands
                # which get sent via PB to the buildslave, and PB doesn't
                # handle unicode.
                path = path.encode("ascii")
                if path.startswith("/"):
                    path = path[1:]
                where = self._transform_path(path)

                # if 'where' is None, the file was outside any project that
                # we care about and we should ignore it
                if where:
                    branch, filename = where
                    if not branch in branches:
                        branches[branch] = { 'files': []}
                    branches[branch]['files'].append(filename)

                    if not branches[branch].has_key('action'):
                        branches[branch]['action'] = action

            for branch in branches.keys():
                action = branches[branch]['action']
                files  = branches[branch]['files']
                number_of_files_changed = len(files)

                if action == u'D' and number_of_files_changed == 1 and files[0] == '':
                    log.msg("Ignoring deletion of branch '%s'" % branch)
                else:
                    chdict = dict(
                            author=author,
                            files=files,
                            comments=comments,
                            revision=revision,
                            branch=branch,
                            revlink=revlink,
                            category=self.category,
                            repository=self.svnurl,
                            project = self.project)
                    changes.append(chdict)

        return changes

    @defer.deferredGenerator
    def submit_changes(self, changes):
        for chdict in changes:
            wfd = defer.waitForDeferred(self.master.addChange(**chdict))
            yield wfd
            wfd.getResult()

    def finished_ok(self, res):
        if self.cachepath:
            f = open(self.cachepath, "w")
            f.write(str(self.last_change))
            f.close()

        log.msg("SVNPoller finished polling %s" % res)
        return res
